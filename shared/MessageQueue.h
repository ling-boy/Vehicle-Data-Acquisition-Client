#pragma once

#include <string>
#include <cstring>
#include <ctime>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstdint>
#include <atomic>

namespace vehicle {

// POSIX 消息队列封装
// 用于进程间传递控制信号（启动、停止、心跳）
class MessageQueue {
public:
    // 创建（由 supervisor 调用，带 O_CREAT）
    static MessageQueue create(const std::string& name, size_t maxMsg = 10, size_t msgSize = 64) {
        MessageQueue mq;
        mq.name_ = name;
        mq.msgSize_ = msgSize;

        struct mq_attr attr{};
        attr.mq_maxmsg  = maxMsg;
        attr.mq_msgsize = msgSize;

        mq.desc_ = mq_open(name.c_str(), O_CREAT | O_RDWR, 0666, &attr);
        if (mq.desc_ == (mqd_t)-1) {
            throw std::runtime_error("mq_open create failed: " + name);
        }
        return mq;
    }

    // 打开已存在的（由子进程调用，不带 O_CREAT）
    static MessageQueue open(const std::string& name, size_t msgSize = 64) {
        MessageQueue mq;
        mq.name_ = name;
        mq.msgSize_ = msgSize;

        mq.desc_ = mq_open(name.c_str(), O_RDWR);
        if (mq.desc_ == (mqd_t)-1) {
            throw std::runtime_error("mq_open open failed: " + name);
        }
        return mq;
    }

    // 移动语义
    MessageQueue(MessageQueue&& o) noexcept
        : desc_(o.desc_), name_(std::move(o.name_)), msgSize_(o.msgSize_) {
        o.desc_ = (mqd_t)-1;
    }

    MessageQueue& operator=(MessageQueue&& o) noexcept {
        if (this != &o) {
            close();
            desc_ = o.desc_;
            name_ = std::move(o.name_);
            msgSize_ = o.msgSize_;
            o.desc_ = (mqd_t)-1;
        }
        return *this;
    }

    ~MessageQueue() { close(); }

    // 发送消息（阻塞，带超时毫秒）
    bool send(const void* data, size_t len, unsigned int priority = 0, int timeoutMs = 1000) {
        if (desc_ == (mqd_t)-1) return false;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeoutMs / 1000;
        ts.tv_nsec += (timeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

        size_t sendLen = len < msgSize_ ? len : msgSize_;
        int ret = mq_timedsend(desc_, static_cast<const char*>(data), sendLen, priority, &ts);
        return ret == 0;
    }

    // 发送 POD 消息的便捷方法
    template<typename T>
    bool sendPod(const T& msg, unsigned int priority = 0, int timeoutMs = 1000) {
        return send(&msg, sizeof(T), priority, timeoutMs);
    }

    // 接收消息（阻塞，带超时毫秒）
    // 返回读取的字节数，0 表示超时或错误
    uint32_t receive(void* buf, size_t bufSize, unsigned int* priority = nullptr, int timeoutMs = 1000) {
        if (desc_ == (mqd_t)-1) return 0;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeoutMs / 1000;
        ts.tv_nsec += (timeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

        ssize_t n = mq_timedreceive(desc_, static_cast<char*>(buf), bufSize, priority, &ts);
        return (n > 0) ? static_cast<uint32_t>(n) : 0;
    }

    // 接收 POD 消息的便捷方法
    template<typename T>
    bool receivePod(T& msg, unsigned int* priority = nullptr, int timeoutMs = 1000) {
        uint32_t n = receive(&msg, sizeof(T), priority, timeoutMs);
        return n >= sizeof(T);
    }

    void close() {
        if (desc_ != (mqd_t)-1) {
            mq_close(desc_);
            desc_ = (mqd_t)-1;
        }
    }

    // 销毁队列（由 supervisor 在清理时调用）
    void unlink() {
        if (!name_.empty()) {
            mq_unlink(name_.c_str());
        }
    }

    mqd_t fd() const { return desc_; }

    // 默认构造（未连接状态，需通过 create/open 初始化）
    MessageQueue() = default;

private:
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    mqd_t      desc_     = (mqd_t)-1;
    std::string name_;
    size_t      msgSize_  = 64;
};

} // namespace vehicle
