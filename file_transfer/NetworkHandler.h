#ifndef NETWORKHANDLER_H
#define NETWORKHANDLER_H

#include "INetworkHandler.h"
#include <string>
#include <functional>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/err.h>

class NetworkHandler : public INetworkHandler
{
public:
    static std::unique_ptr<NetworkHandler> createNew(
        const std::string &server, int port,
        bool use_tls = false, const std::string &ca_cert = "");

    NetworkHandler(const std::string &server, int port,
                   bool use_tls = false, const std::string &ca_cert = "");
    ~NetworkHandler();

    bool connect() override;
    void close() override;
    void sendFile(IFileHandler &file_handler) override;
    bool isConnected() const;

    void setDisconnectCallback(const std::function<void()> &callback);

private:
    // 统一发送方法（TLS 和非 TLS 共用）
    int sendData(const void *data, int len);
    int recvData(void *buf, int len);

    // 原有成员
    std::string server_;
    int port_;
    int sockfd_;
    bool connected_ = false;
    std::function<void()> onDisconnect_;
    static std::mutex mtx;

    // TLS 成员
    SSL_CTX *ssl_ctx_ = nullptr;
    SSL *ssl_ = nullptr;
    bool use_tls_ = false;
    std::string ca_cert_path_;
};

#endif // NETWORKHANDLER_H
