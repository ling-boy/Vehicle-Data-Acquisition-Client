#include "DataProcessing.h"
#include "../shared/SharedQueue.h"
#include "../shared/Logger.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

DataProcessing::DataProcessing() {}
DataProcessing::~DataProcessing() {}

std::unique_ptr<DataProcessing> DataProcessing::createNew()
{
    return std::make_unique<DataProcessing>();
}

bool DataProcessing::processDirectories()
{
    // 从共享内存环形缓冲区读取 SensorData
    if (!g_collectRing) {
        LOG_ERROR("g_collectRing is null, cannot read from shared memory");
        return false;
    }

    LOG_INFO("DataProcessing started, reading from shared memory ring buffer");

    while (cKeepRunning)
    {
        // 从共享内存读取一条 SensorData
        SensorData tempData;
        uint32_t bytesRead = g_collectRing->read(&tempData, sizeof(SensorData));

        if (bytesRead == 0) {
            // 共享内存为空，等待一小段时间再试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 解析时间戳，做时间窗口分桶
        std::string ts = tempData.getTimestamp();
        if (ts.empty() || ts.size() < 23) {
            continue;  // 无效时间戳，跳过
        }

        std::string date = ts.substr(0, 10); // "YYYY-MM-DD"
        std::string time = ts.substr(11);    // "HH-MM-SS-sss"
        int milliseconds = std::stoi(time.substr(9, 3));
        int timeWindowIndex = milliseconds / TIME_WINDOW_MS * TIME_WINDOW_MS;

        std::ostringstream oss;
        oss << time.substr(0, 8) << "-" << std::setfill('0') << std::setw(3) << timeWindowIndex;
        std::string timeWindowStr = oss.str();

        // 创建目标目录结构
        std::string basePath = "Dataset/Car0001/" + date + "/" + timeWindowStr;
        if (!fs::exists(basePath))
        {
            fs::create_directories(basePath);
            std::vector<std::string> sensorTypes = {"audio", "camera", "imu"};
            for (const auto& sensorType : sensorTypes)
            {
                std::string sensorFolderPath = basePath + "/" + sensorType;
                if (!fs::exists(sensorFolderPath)) {
                    fs::create_directories(sensorFolderPath);
                }
            }
        }

        // 移动文件到对应的传感器子目录
        std::string sensorFolderPath = basePath + "/" + vehicle::sensorTypeToString(tempData.sensorType);
        if (!fs::exists(sensorFolderPath)) {
            fs::create_directories(sensorFolderPath);
        }

        fs::path sourceFilePath(tempData.getFilePath());
        fs::path destinationFilePath = fs::path(sensorFolderPath) / sourceFilePath.filename();

        try {
            if (fs::exists(sourceFilePath)) {
                fs::rename(sourceFilePath, destinationFilePath);
                LOG_DEBUG("Moved " << vehicle::sensorTypeToString(tempData.sensorType)
                          << " file to " << destinationFilePath.string());
            }
        }
        catch (const fs::filesystem_error &e) {
            LOG_ERROR("Error moving file: " << e.what());
        }

        // 把处理后的目录路径写入 process→send 共享内存
        if (g_processRing) {
            std::string pathStr = basePath;
            g_processRing->write(pathStr.c_str(), pathStr.size() + 1);
        }
    }
    return true;
}
