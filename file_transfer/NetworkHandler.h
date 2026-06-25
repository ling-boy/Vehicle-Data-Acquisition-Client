#ifndef NETWORKHANDLER_H
#define NETWORKHANDLER_H

#include "INetworkHandler.h"
#include <string>
#include <functional> // Ϊ��ʹ�ûص�����
#include <mutex>

class NetworkHandler : public INetworkHandler
{
public:
    static std::unique_ptr<NetworkHandler> createNew(const std::string &server, int port);
    NetworkHandler(const std::string &server, int port);
    ~NetworkHandler();  // 确保 socket 关闭

    bool connect() override;
    void close() override;
    void sendFile(IFileHandler &file_handler) override;
    bool isConnected() const; // ����Ƿ�����

    // ���öϿ�����ʱ�Ļص�����
    void setDisconnectCallback(const std::function<void()> &callback);

private:
    std::string server_;
    int port_;
    int sockfd_;
    bool connected_ = false;             // ��¼��ǰ����״̬
    std::function<void()> onDisconnect_; // �Ͽ����ӵĻص�����
    static std::mutex mtx;               // �����ļ�ʱ���߳�ͬ��
};

#endif // NETWORKHANDLER_H
