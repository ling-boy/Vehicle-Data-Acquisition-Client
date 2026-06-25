#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <opencv2/opencv.hpp>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include "../shared/SharedQueue.h"

namespace fs = std::filesystem;

class CameraThreadManager {
public:
    CameraThreadManager(const std::vector<std::string>& devicePaths);
    ~CameraThreadManager();

    void start();
    void stop();

    struct ThreadInfo {
        std::thread::id threadID;
        std::string deviceID;
        std::string threadName;
    };

    void onDeviceChange(const std::vector<std::string>& newDevicePaths, const std::vector<std::string>& offDevicePaths);
    const std::vector<ThreadInfo>& getThreadInfoList() const;

private:
    // 帧缓冲区池：预分配原始内存，避免每帧 malloc/free
    struct FrameBufferPool {
        std::vector<std::vector<uint8_t>> rawBuffers;  // 预分配的原始缓冲区
        std::vector<cv::Mat> frames;                    // 指向缓冲区的 cv::Mat 头
        std::queue<int> freeIndices;                    // 空闲索引队列
        std::mutex mu;
        bool initialized = false;

        void init(int count, int rows, int cols, int type) {
            std::lock_guard<std::mutex> lk(mu);
            size_t elemSize = CV_ELEM_SIZE(type);
            size_t step = cols * elemSize;
            for (int i = 0; i < count; i++) {
                rawBuffers.emplace_back(rows * step);
                frames.emplace_back(rows, cols, type, rawBuffers.back().data(), step);
                freeIndices.push(i);
            }
            initialized = true;
        }

        // 获取一个空闲缓冲区索引，-1 表示池满
        int acquire() {
            std::lock_guard<std::mutex> lk(mu);
            if (freeIndices.empty()) return -1;
            int idx = freeIndices.front();
            freeIndices.pop();
            return idx;
        }

        // 归还缓冲区
        void release(int idx) {
            std::lock_guard<std::mutex> lk(mu);
            freeIndices.push(idx);
        }

        int freeCount() {
            std::lock_guard<std::mutex> lk(mu);
            return freeIndices.size();
        }
    };

    // 帧队列元素：(缓冲区索引, 帧序号)
    using FrameEntry = std::pair<int, int>;

    void captureFrames(const std::string& devicePath, std::queue<FrameEntry>& frameQueue, FrameBufferPool& pool);
    void saveFramesWorker(std::queue<FrameEntry>& frameQueue, const std::string& cameraName, FrameBufferPool& pool);
    std::string getCurrentDateTimeString();
    void saveFrames(std::queue<FrameEntry>& frameQueue, const std::string& currentDateTime, const std::string& cameraName, FrameBufferPool& pool);
    void stopThread(std::thread& t);

    std::vector<std::string> devicePaths;
    std::vector<std::queue<FrameEntry>> frameQueues;
    std::vector<std::unique_ptr<FrameBufferPool>> framePools;  // 每个摄像头一个帧池
    std::vector<std::thread> captureThreads;
    std::vector<std::thread> saveThreads;

    std::mutex queueMutex;
    std::condition_variable dataCondition;
    bool keepRunning = true;
    std::vector<bool> saveThreadsRunning;

    std::vector<ThreadInfo> threadInfoList;
};
