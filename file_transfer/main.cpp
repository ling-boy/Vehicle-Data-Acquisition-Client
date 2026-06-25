// 文件传输进程入口
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include "../shared/Logger.h"
#include "../shared/Config.h"
#include "../shared/SignalHandler.h"
#include "../shared/ShmRingBuffer.h"
#include "../shared/MessageQueue.h"
#include "../shared/SensorData.h"
#include "../shared/Types.h"
#include "FileSendingTask.h"

using namespace vehicle;

// g_transferRing 定义在 SharedQueue.cpp (通过 SharedQueue.h 的 extern)
std::atomic<bool> g_running{true};

int main(int argc, char* argv[]) {
    Logger::instance().init("transfer", "logs", LogLevel::INFO);
    LOG_INFO("=== File Transfer Process starting ===");

    std::string cfg = argc > 1 ? argv[1] : "config/vehicle.conf";
    Config::instance().load(cfg);

    SignalHandler::instance().initialize();
    SignalHandler::instance().onSignal(SIGTERM, [](int){ g_running = false; });
    SignalHandler::instance().onSignal(SIGINT,  [](int){ g_running = false; });

    try {
        g_transferRing = ShmRingBuffer::open(SHM_PROCESS_TO_SEND);
        LOG_INFO("Shared memory ring buffer opened");
    } catch (const std::exception& e) {
        LOG_FATAL("IPC open failed: " << e.what());
        return 1;
    }

    std::string server = Config::instance().getString("server.host", "tstit.x3322.net");
    int port = Config::instance().getInt("server.port", 12345);
    bool use_tls = Config::instance().getBool("server.use_tls", false);
    std::string ca_cert = Config::instance().getString("server.ca_cert", "");

    if (use_tls) {
        LOG_INFO("TLS enabled, CA cert: " << (ca_cert.empty() ? "none (insecure)" : ca_cert));
    }

    fileSendingTask(server, port, use_tls, ca_cert);

    delete g_transferRing;
    LOG_INFO("=== File Transfer Process stopped ===");
    return 0;
}
