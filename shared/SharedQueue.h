#pragma once

// ============================================================================
// SharedQueue - 兼容层
// 保留原有 API 签名，底层使用 ShmRingBuffer + MessageQueue
// 新代码应直接使用 ShmRingBuffer / MessageQueue / Logger
// ============================================================================

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "Types.h"
#include "SensorData.h"
#include "Logger.h"
#include "ShmRingBuffer.h"

// 兼容旧代码：将 vehicle 命名空间的类型引入全局
using vehicle::SensorData;
using vehicle::ShmRingBuffer;

// ---- 兼容旧代码的全局变量 ----
// 采集进程内部仍使用 std::queue 做线程间缓冲，
// 由 ProcessingTask 从队列取出后写入 ShmRingBuffer

// 采集→处理 队列（进程内线程间）
extern std::queue<SensorData>        captureToProcessingQueue;
extern std::mutex                    captureToProcessingQueueMutex;
extern std::condition_variable       captureToProcessingQueueCondition;

// 处理→传输 队列（进程内线程间）
extern std::queue<std::string>       processingToSendingQueue;
extern std::mutex                    processingToSendingQueueMutex;
extern std::condition_variable       processingToSendingQueueCondition;

// 全局运行标志
extern std::atomic<bool>             cKeepRunning;

// 全局 IPC 通道指针（由各进程 main.cpp 初始化）
// 为 nullptr 时退化为进程内 std::queue 模式
extern ShmRingBuffer* g_collectRing;   // 采集→处理 共享内存
extern ShmRingBuffer* g_processRing;   // 处理→传输 共享内存
extern ShmRingBuffer* g_transferRing;  // 传输进程 共享内存
