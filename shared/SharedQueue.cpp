#include "SharedQueue.h"

// 兼容旧代码的全局变量定义
std::queue<SensorData>        captureToProcessingQueue;
std::mutex                    captureToProcessingQueueMutex;
std::condition_variable       captureToProcessingQueueCondition;

std::queue<std::string>       processingToSendingQueue;
std::mutex                    processingToSendingQueueMutex;
std::condition_variable       processingToSendingQueueCondition;

std::atomic<bool>             cKeepRunning{true};

// IPC 通道指针（默认 nullptr，由各进程 main 设置）
ShmRingBuffer* g_collectRing = nullptr;
ShmRingBuffer* g_processRing = nullptr;
ShmRingBuffer* g_transferRing = nullptr;
