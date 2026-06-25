#ifndef FILESEND_H
#define FILESEND_H

#include <iostream>
#include <thread>
#include "FileHandler.h"
#include "NetworkHandler.h"
#include "../shared/SharedQueue.h"

class FileSender
{
public:
    static std::unique_ptr<FileSender> createNew(const std::string &server, int port,
                                                  bool use_tls = false, const std::string &ca_cert = "");
    FileSender(const std::string &server, int port,
               bool use_tls = false, const std::string &ca_cert = "");
    bool start(fs::path dir_path);
    bool reconnect();

private:
    std::string server_;
    int port_;
    bool isConnected_;
    bool use_tls_;
    std::string ca_cert_;
    std::unique_ptr<NetworkHandler> network_handler_;
};

#endif // FILESEND_H
