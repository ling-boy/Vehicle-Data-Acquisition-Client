#include "CameraThreadManager.h"
#include "../shared/Logger.h"

CameraThreadManager::CameraThreadManager(const std::vector<std::string> &devicePaths)
    : devicePaths(devicePaths), frameQueues(devicePaths.size())
{
}

CameraThreadManager::~CameraThreadManager()
{
    stop();
}

void CameraThreadManager::start()
{
    keepRunning = true;
    for (size_t i = 0; i < devicePaths.size(); ++i)
    {
        framePools.push_back(std::make_unique<FrameBufferPool>());
        captureThreads.emplace_back(&CameraThreadManager::captureFrames, this,
            devicePaths[i], std::ref(frameQueues[i]), std::ref(*framePools[i]));

        threadInfoList.push_back({captureThreads.back().get_id(), devicePaths[i], "captureThread"});

        std::string cameraName = "camera" + std::to_string(i);
        saveThreadsRunning.push_back(true);
        saveThreads.emplace_back(&CameraThreadManager::saveFramesWorker, this,
            std::ref(frameQueues[i]), cameraName, std::ref(*framePools[i]));
        saveThreads[i].detach();
        threadInfoList.push_back({saveThreads.back().get_id(), devicePaths[i], "saveThread"});
    }
}

void CameraThreadManager::stop()
{
    keepRunning = false;
    dataCondition.notify_all();

    for (auto &t : captureThreads)
    {
        if (t.joinable()) t.join();
    }
    for (auto &t : saveThreads)
    {
        if (t.joinable()) t.join();
    }
    captureThreads.clear();
    saveThreads.clear();
}

const std::vector<CameraThreadManager::ThreadInfo> &CameraThreadManager::getThreadInfoList() const
{
    return threadInfoList;
}

void CameraThreadManager::captureFrames(const std::string &devicePath,
                                         std::queue<FrameEntry> &frameQueue,
                                         FrameBufferPool &pool)
{
cvInit:
    cv::VideoCapture cap(devicePath);
    if (!cap.isOpened())
    {
        LOG_ERROR("Could not open camera at " << devicePath);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return;
    }

    // 初始化帧池：先抓一帧获取分辨率
    if (!pool.initialized) {
        cv::Mat testFrame;
        cap >> testFrame;
        if (!testFrame.empty()) {
            pool.init(5, testFrame.rows, testFrame.cols, testFrame.type());
            LOG_INFO("Frame pool initialized for " << devicePath
                      << ": " << testFrame.cols << "x" << testFrame.rows
                      << ", 5 buffers");
        }
    }

    int frameNumber = 0;
    int ErrCapCount = 0;
    while (keepRunning)
    {
        // 从池中获取空闲缓冲区
        int bufIdx = pool.acquire();
        if (bufIdx < 0) {
            // 池满，跳过这一帧
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 直接写入预分配的缓冲区（不 malloc）
        cap >> pool.frames[bufIdx];

        if (pool.frames[bufIdx].empty())
        {
            pool.release(bufIdx);  // 归还空缓冲区
            LOG_ERROR("Captured empty frame from " << devicePath);
            if (5 != ErrCapCount)
            {
                ErrCapCount++;
                continue;
            }
            else
            {
                ErrCapCount = 0;
                goto cvInit;
            }
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            frameQueue.push({bufIdx, frameNumber++});
            dataCondition.notify_one();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void CameraThreadManager::saveFramesWorker(std::queue<FrameEntry> &frameQueue,
                                            const std::string &cameraName,
                                            FrameBufferPool &pool)
{
    size_t threadIndex = 0;
    for (size_t i = 0; i < saveThreads.size(); ++i)
    {
        if (saveThreads[i].get_id() == std::this_thread::get_id())
        {
            threadIndex = i;
            break;
        }
    }

    while (true)
    {
        std::string currentDateTime;
        std::unique_lock<std::mutex> lock(queueMutex);

        dataCondition.wait(lock, [&frameQueue, this, threadIndex]
                           { return !frameQueue.empty() || !saveThreadsRunning[threadIndex]; });

        if (!saveThreadsRunning[threadIndex] && frameQueue.empty())
        {
            break;
        }

        if (!frameQueue.empty())
        {
            currentDateTime = getCurrentDateTimeString();
            saveFrames(frameQueue, currentDateTime, cameraName, pool);
        }
    }
}

std::string CameraThreadManager::getCurrentDateTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&now_time_t, &tm);

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d/%H-%M-%S") << '-' << std::setfill('0') << std::setw(3) << now_ms.count();
    return ss.str();
}

void CameraThreadManager::saveFrames(std::queue<FrameEntry> &frameQueue,
                                      const std::string &currentDateTime,
                                      const std::string &cameraName,
                                      FrameBufferPool &pool)
{
    std::string baseDir = std::filesystem::current_path().string();
    std::string carNumber = "0001";
    std::string curDateTime = currentDateTime.substr(0, 19);
    std::string msTime = currentDateTime.substr(20, 23);

    std::string folderPath = baseDir + "/dataCapture/Car" + carNumber + "/Camera/" + curDateTime;
    fs::create_directories(folderPath);

    while (!frameQueue.empty())
    {
        auto [bufIdx, frameNumber] = frameQueue.front();
        frameQueue.pop();

        std::string filename = folderPath + "/" + cameraName + "-" + msTime + ".jpg";
        cv::imwrite(filename, pool.frames[bufIdx]);

        // 归还缓冲区到池（不 free）
        pool.release(bufIdx);

        // 从对象池获取 SensorData
        vehicle::SensorData* data = g_sensorDataPool ? g_sensorDataPool->acquire() : new vehicle::SensorData();
        data->sensorType = vehicle::SensorType::CAMERA;
        data->setTimestamp(currentDateTime);
        data->setFilePath(filename);
        {
            std::lock_guard<std::mutex> lock(captureToProcessingQueueMutex);
            captureToProcessingQueue.push(data);
            captureToProcessingQueueCondition.notify_one();
        }
    }
}

void CameraThreadManager::onDeviceChange(const std::vector<std::string> &newDevicePaths,
                                          const std::vector<std::string> &offDevicePaths)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    LOG_INFO("Device change detected, updating threads...");

    for (const auto &device : newDevicePaths)
    {
        if (std::find(devicePaths.begin(), devicePaths.end(), device) == devicePaths.end())
        {
            frameQueues.emplace_back();
            framePools.push_back(std::make_unique<FrameBufferPool>());
            devicePaths.push_back(device);

            size_t index = devicePaths.size() - 1;
            captureThreads.emplace_back(&CameraThreadManager::captureFrames, this,
                devicePaths[index], std::ref(frameQueues[index]), std::ref(*framePools[index]));
            saveThreadsRunning.push_back(true);
            saveThreads.emplace_back(&CameraThreadManager::saveFramesWorker, this,
                std::ref(frameQueues[index]), "camera" + std::to_string(index), std::ref(*framePools[index]));
            saveThreads[index].detach();

            ThreadInfo captureInfo = {captureThreads.back().get_id(), devicePaths[index], "captureThread"};
            ThreadInfo saveInfo = {saveThreads.back().get_id(), devicePaths[index], "saveThread"};
            threadInfoList.push_back(captureInfo);
            threadInfoList.push_back(saveInfo);

            LOG_INFO("Added new device and threads for: " << device);
        }
    }

    for (const auto &device : offDevicePaths)
    {
        auto it = std::find(devicePaths.begin(), devicePaths.end(), device);
        if (it != devicePaths.end())
        {
            const size_t index = std::distance(devicePaths.begin(), it);
            LOG_INFO("Stopping threads for device: " << device);

            stopThread(captureThreads[index]);
            saveThreadsRunning[index] = false;
            dataCondition.notify_all();

            captureThreads.erase(captureThreads.begin() + index);
            saveThreads.erase(saveThreads.begin() + index);
            frameQueues.erase(frameQueues.begin() + index);
            framePools.erase(framePools.begin() + index);
            threadInfoList.erase(threadInfoList.begin() + 2 * index, threadInfoList.begin() + 2 * index + 2);
            saveThreadsRunning.erase(saveThreadsRunning.begin() + index);
            devicePaths.erase(it);

            LOG_INFO("Removed device and threads for: " << device);
        }
    }
}

void CameraThreadManager::stopThread(std::thread &t)
{
    try
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    catch (const std::system_error &e)
    {
        LOG_ERROR("Error joining thread: " << e.what());
    }
}
