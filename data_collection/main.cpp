// 采集进程入口
#include <iostream>
#include <thread>
#include <atomic>
#include "../shared/Logger.h"
#include "../shared/Config.h"
#include "../shared/SignalHandler.h"
#include "../shared/ShmRingBuffer.h"
#include "../shared/MessageQueue.h"
#include "../shared/SensorData.h"
#include "../shared/MemoryPool.h"
#include "../shared/Types.h"
#include "DataCollector.h"

using namespace vehicle;

// 全局：采集进程使用的环形缓冲区指针（供各传感器驱动写入）
// g_collectRing 定义在 SharedQueue.cpp
std::atomic<bool> g_running{true};

int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::instance().init("collect", "logs", LogLevel::INFO);
    LOG_INFO("=== Data Collection Process starting ===");
    std::cerr << "[collect] Logger initialized, PID=" << getpid() << std::endl;

    // 加载配置
    std::string cfg = argc > 1 ? argv[1] : "config/vehicle.conf";
    Config::instance().load(cfg);

    // 初始化信号
    SignalHandler::instance().initialize();
    SignalHandler::instance().onSignal(SIGTERM, [](int){ g_running = false; });
    SignalHandler::instance().onSignal(SIGINT,  [](int){ g_running = false; });

    // 连接共享内存
    try {
        g_collectRing = ShmRingBuffer::open(SHM_COLLECT_TO_PROCESS);
        LOG_INFO("Shared memory ring buffer opened");
    } catch (const std::exception& e) {
        LOG_FATAL("IPC open failed: " << e.what());
        return 1;
    }

    // 创建 SensorData 对象池（256 个预分配槽位）
    MemoryPool<SensorData> sensorDataPool(256);
    g_sensorDataPool = &sensorDataPool;
    LOG_INFO("SensorData pool created, capacity=256");

    // 创建并启动采集器
    auto collector = DataCollector::createNew();
    if (!collector->DataCollectorLoopStart()) {
        LOG_ERROR("DataCollector failed");
    }

    // 清理
    delete g_collectRing;
    LOG_INFO("=== Data Collection Process stopped ===");
    return 0;
}
