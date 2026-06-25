#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace vehicle {

using Timestamp = std::chrono::steady_clock::time_point;
using Duration  = std::chrono::steady_clock::duration;

enum class SensorType : uint8_t {
    CAMERA  = 0,
    AUDIO   = 1,
    IMU     = 2,
    RADAR   = 3,
    UNKNOWN = 0xFF
};

inline const char* sensorTypeToString(SensorType type) {
    switch (type) {
        case SensorType::CAMERA: return "camera";
        case SensorType::AUDIO:  return "audio";
        case SensorType::IMU:    return "imu";
        case SensorType::RADAR:  return "radar";
        default:                 return "unknown";
    }
}

inline SensorType stringToSensorType(const std::string& str) {
    if (str == "camera") return SensorType::CAMERA;
    if (str == "audio")  return SensorType::AUDIO;
    if (str == "imu")    return SensorType::IMU;
    if (str == "radar")  return SensorType::RADAR;
    return SensorType::UNKNOWN;
}

// IPC通道名称
constexpr const char* SHM_COLLECT_TO_PROCESS = "/veh_collect2process";
constexpr const char* SHM_PROCESS_TO_SEND    = "/veh_process2send";
constexpr const char* MQ_CONTROL_FMT         = "/veh_ctrl_%s";

// 缓冲区默认大小
constexpr size_t DEFAULT_RING_BUFFER_CAPACITY = 1024;
constexpr size_t DEFAULT_MAX_MSG_SIZE         = 4096;

// 重试参数
constexpr int DEFAULT_MAX_RETRIES    = 3;
constexpr int DEFAULT_RETRY_DELAY_MS = 1000;

} // namespace vehicle
