// 数据处理进程入口
#include <iostream>
#include <thread>
#include <atomic>
#include "../shared/Logger.h"
#include "../shared/Config.h"
#include "../shared/SignalHandler.h"
#include "../shared/ShmRingBuffer.h"
#include "../shared/MessageQueue.h"
#include "../shared/SensorData.h"
#include "../shared/Types.h"
#include "DataProcessing.h"

using namespace vehicle;

// g_collectRing 和 g_processRing 定义在 SharedQueue.cpp
std::atomic<bool> g_running{true};

int main(int argc, char* argv[]) {
    Logger::instance().init("process", "logs", LogLevel::INFO);
    LOG_INFO("=== Data Process Process starting ===");

    std::string cfg = argc > 1 ? argv[1] : "config/vehicle.conf";
    Config::instance().load(cfg);

    SignalHandler::instance().initialize();
    SignalHandler::instance().onSignal(SIGTERM, [](int){ g_running = false; });
    SignalHandler::instance().onSignal(SIGINT,  [](int){ g_running = false; });

    try {
        // 打开两个共享内存：collect→process 和 process→send
        g_collectRing = ShmRingBuffer::open(SHM_COLLECT_TO_PROCESS);
        g_processRing = ShmRingBuffer::open(SHM_PROCESS_TO_SEND);
        LOG_INFO("Shared memory ring buffers opened");
    } catch (const std::exception& e) {
        LOG_FATAL("IPC open failed: " << e.what());
        return 1;
    }

    auto processor = DataProcessing::createNew();
    if (!processor->processDirectories()) {
        LOG_ERROR("DataProcessing failed");
    }

    delete g_collectRing;
    delete g_processRing;
    LOG_INFO("=== Data Process Process stopped ===");
    return 0;
}
