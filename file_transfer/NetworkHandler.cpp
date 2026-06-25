#include "NetworkHandler.h"
#include "../shared/Logger.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <iostream>
#include <fstream>

std::mutex NetworkHandler::mtx;
const size_t CHUNK_SIZE = 4096;

std::unique_ptr<NetworkHandler> NetworkHandler::createNew(
    const std::string &server, int port,
    bool use_tls, const std::string &ca_cert)
{
    return std::make_unique<NetworkHandler>(server, port, use_tls, ca_cert);
}

NetworkHandler::NetworkHandler(const std::string &server, int port,
                               bool use_tls, const std::string &ca_cert)
    : server_(server), port_(port), sockfd_(-1),
      use_tls_(use_tls), ca_cert_path_(ca_cert) {}

NetworkHandler::~NetworkHandler() {
    close();
}

bool NetworkHandler::connect()
{
    struct addrinfo hints, *res;
    int status;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(server_.c_str(), std::to_string(port_).c_str(), &hints, &res)) != 0)
    {
        LOG_ERROR("getaddrinfo: " << gai_strerror(status));
        return false;
    }
    sockfd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd_ == -1)
    {
        LOG_ERROR("Error creating socket");
        freeaddrinfo(res);
        return false;
    }

    if (::connect(sockfd_, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("Connection Failed");
        close();
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    // TLS 握手
    if (use_tls_) {
        ssl_ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_) {
            LOG_ERROR("SSL_CTX_new failed");
            close();
            return false;
        }

        // 加载 CA 证书验证服务端身份
        if (!ca_cert_path_.empty()) {
            if (SSL_CTX_load_verify_locations(ssl_ctx_, ca_cert_path_.c_str(), nullptr) != 1) {
                LOG_ERROR("Failed to load CA cert: " << ca_cert_path_);
                close();
                return false;
            }
            // 启用服务端证书验证
            SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
        } else {
            // 不验证证书（测试环境）
            SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);
            LOG_WARN("TLS: no CA cert, server certificate NOT verified");
        }

        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            LOG_ERROR("SSL_new failed");
            close();
            return false;
        }

        SSL_set_fd(ssl_, sockfd_);
        SSL_set_connect_state(ssl_);

        int ret = SSL_connect(ssl_);
        if (ret <= 0) {
            int err = SSL_get_error(ssl_, ret);
            LOG_ERROR("TLS handshake failed, SSL error: " << err);
            ERR_print_errors_fp(stderr);
            close();
            return false;
        }

        LOG_INFO("TLS connected: " << SSL_get_version(ssl_)
                  << " cipher=" << SSL_get_cipher(ssl_));
    }

    connected_ = true;
    return true;
}

void NetworkHandler::close()
{
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    if (sockfd_ != -1) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
    connected_ = false;
}

bool NetworkHandler::isConnected() const
{
    return connected_;
}

int NetworkHandler::sendData(const void *data, int len)
{
    if (use_tls_ && ssl_) {
        return SSL_write(ssl_, data, len);
    } else {
        return send(sockfd_, data, len, 0);
    }
}

int NetworkHandler::recvData(void *buf, int len)
{
    if (use_tls_ && ssl_) {
        return SSL_read(ssl_, buf, len);
    } else {
        return recv(sockfd_, buf, len, 0);
    }
}

void NetworkHandler::sendFile(IFileHandler &file_handler)
{
    if (!isConnected())
    {
        LOG_ERROR("Not connected to server");
        if (onDisconnect_) onDisconnect_();
        return;
    }

    try
    {
        std::string file_name = file_handler.fileName();
        uint32_t name_length = htonl(file_name.size());
        if (sendData(&name_length, sizeof(name_length)) < 0)
            throw std::runtime_error("Failed to send name length");
        if (sendData(file_name.c_str(), file_name.size()) < 0)
            throw std::runtime_error("Failed to send file name");

        size_t file_size = file_handler.fileSize();
        uint32_t net_file_size = htonl(static_cast<uint32_t>(file_size));
        if (sendData(&net_file_size, sizeof(net_file_size)) < 0)
            throw std::runtime_error("Failed to send file size");

        std::ifstream file(file_handler.fileName(), std::ios::binary);
        std::vector<char> buffer(CHUNK_SIZE);
        size_t total_bytes_sent = 0;

        while (total_bytes_sent < file_size)
        {
            file.read(buffer.data(), CHUNK_SIZE);
            std::streamsize bytes_read = file.gcount();
            if (bytes_read > 0)
            {
                if (sendData(buffer.data(), bytes_read) <= 0)
                    throw std::runtime_error("Failed to send file data");

                total_bytes_sent += bytes_read;

                std::lock_guard<std::mutex> lock(mtx);
                std::cout << "\rProgress: " << (100 * total_bytes_sent / file_size) << "%" << std::flush;
            }
        }
        std::cout << std::endl;

        // TLS 模式下不发送 MD5（TLS HMAC 已保证完整性）
        if (!use_tls_) {
            std::string md5_hash = file_handler.calculateMd5();
            uint32_t hash_length = htonl(md5_hash.size());
            if (sendData(&hash_length, sizeof(hash_length)) < 0)
                throw std::runtime_error("Failed to send hash length");
            if (sendData(md5_hash.c_str(), md5_hash.size()) < 0)
                throw std::runtime_error("Failed to send MD5 hash");

            std::lock_guard<std::mutex> lock(mtx);
            std::cout << "Sent file: " << file_name << " (" << file_size << " bytes), MD5: " << md5_hash << std::endl;
        } else {
            std::lock_guard<std::mutex> lock(mtx);
            std::cout << "Sent file (TLS): " << file_name << " (" << file_size << " bytes)" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error during file sending: " << e.what());
        if (onDisconnect_) onDisconnect_();
    }
}

void NetworkHandler::setDisconnectCallback(const std::function<void()> &callback)
{
    onDisconnect_ = callback;
}
