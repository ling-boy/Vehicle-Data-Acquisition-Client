// Supervisor 主进程入口
// 管理采集、处理、传输三个子进程的生命周期
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "shared/Logger.h"
#include "shared/Config.h"
#include "shared/SignalHandler.h"
#include "shared/ShmRingBuffer.h"
#include "shared/MessageQueue.h"
#include "shared/Types.h"
#include "shared/SensorData.h"
#include "supervisor/ProcessManager.h"

using namespace vehicle;

int main(int argc, char* argv[])
{
    // 1. 初始化日志
    Logger::instance().init("supervisor", "logs", LogLevel::INFO);
    LOG_INFO("=== Vehicle Client Supervisor starting ===");

    // 2. 加载配置
    std::string configFile = "config/vehicle.conf";
    if (argc > 1) configFile = argv[1];
    if (!Config::instance().load(configFile)) {
        LOG_ERROR("Failed to load config: " << configFile);
        return 1;
    }
    LOG_INFO("Config loaded: " << configFile);

    // 3. 初始化信号处理
    SignalHandler::instance().initialize();
    LOG_INFO("Signal handler initialized");

    // 4. 创建共享内存环形缓冲区
    ShmRingBuffer* collectToProcess = nullptr;
    ShmRingBuffer* processToSend    = nullptr;
    try {
        size_t cap = Config::instance().getInt("buffer.ring_capacity", 1024);
        shm_unlink(SHM_COLLECT_TO_PROCESS);
        shm_unlink(SHM_PROCESS_TO_SEND);
        collectToProcess = ShmRingBuffer::create(SHM_COLLECT_TO_PROCESS, cap * sizeof(SensorData));
        processToSend    = ShmRingBuffer::create(SHM_PROCESS_TO_SEND,    cap * 256);
        LOG_INFO("Shared memory ring buffers created, capacity=" << cap);
    } catch (const std::exception& e) {
        LOG_FATAL("Failed to create shared memory: " << e.what());
        return 1;
    }

    // 5. 创建控制消息队列
    MessageQueue mqCollect, mqProcess, mqTransfer;
    try {
        mqCollect  = MessageQueue::create("/veh_ctrl_collect");
        mqProcess  = MessageQueue::create("/veh_ctrl_process");
        mqTransfer = MessageQueue::create("/veh_ctrl_transfer");
        LOG_INFO("Control message queues created");
    } catch (const std::exception& e) {
        LOG_FATAL("Failed to create message queues: " << e.what());
        return 1;
    }

    // 6. 注册子进程
    ProcessManager pm;
    pm.registerProcess("data_collection", "./VehicleCollect", 5);
    pm.registerProcess("data_process",    "./VehicleProcess", 5);
    pm.registerProcess("file_transfer",   "./VehicleTransfer", 5);

    // 7. 注册信号回调
    SignalHandler::instance().onSignal(SIGTERM, [](int) {
        LOG_INFO("Received SIGTERM, initiating shutdown...");
    });
    SignalHandler::instance().onSignal(SIGINT, [](int) {
        LOG_INFO("Received SIGINT, initiating shutdown...");
    });

    // 8. 启动所有子进程
    if (!pm.startAll()) {
        LOG_FATAL("Failed to start all child processes");
        pm.shutdown();
        return 1;
    }
    LOG_INFO("All child processes started");

    // 9. 主循环：监控子进程 + 等待信号
    while (!SignalHandler::instance().shouldExit()) {
        pm.monitorOnce(500);
    }

    // 10. 优雅关停
    LOG_INFO("Initiating graceful shutdown...");
    pm.disableAutoRestart();

    // 通知各子进程停止
    IPCMessage stopMsg(IPCMessageType::STOP);
    mqCollect.sendPod(stopMsg);
    mqProcess.sendPod(stopMsg);
    mqTransfer.sendPod(stopMsg);

    pm.shutdown();

    // 清理资源
    delete collectToProcess;
    delete processToSend;
    mqCollect.unlink();
    mqProcess.unlink();
    mqTransfer.unlink();
    shm_unlink(SHM_COLLECT_TO_PROCESS);
    shm_unlink(SHM_PROCESS_TO_SEND);

    LOG_INFO("=== Vehicle Client Supervisor stopped ===");
    return 0;
}
