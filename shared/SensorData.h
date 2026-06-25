#pragma once

#include <cstring>
#include <string>
#include <cstdint>
#include "Types.h"

namespace vehicle {

// 传感器数据元信息（通过共享内存传递的 POD 结构）
struct SensorData {
    SensorType  sensorType = SensorType::UNKNOWN;
    char        timestamp[64]{};
    char        filePath[256]{};
    uint32_t    fileSize    = 0;
    uint32_t    sequenceNum = 0;

    SensorData() = default;

    SensorData(SensorType type, const std::string& ts, const std::string& path)
        : sensorType(type), fileSize(0), sequenceNum(0) {
        setTimestamp(ts);
        setFilePath(path);
    }

    void setTimestamp(const std::string& ts) {
        size_t n = ts.size() < 63 ? ts.size() : 63;
        memcpy(timestamp, ts.c_str(), n); timestamp[n] = '\0';
    }

    void setFilePath(const std::string& path) {
        size_t n = path.size() < 255 ? path.size() : 255;
        memcpy(filePath, path.c_str(), n); filePath[n] = '\0';
    }

    std::string getTimestamp() const { return std::string(timestamp); }
    std::string getFilePath()  const { return std::string(filePath); }
};

// IPC 控制消息类型
enum class IPCMessageType : uint8_t {
    DATA_READY = 1,
    STOP       = 2,
    HEARTBEAT  = 3,
    ACK        = 4,
};

struct IPCMessage {
    IPCMessageType type    = IPCMessageType::HEARTBEAT;
    uint32_t       payload = 0;

    IPCMessage() = default;
    IPCMessage(IPCMessageType t, uint32_t p = 0) : type(t), payload(p) {}
};

} // namespace vehicle
