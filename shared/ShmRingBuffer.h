#pragma once

#include <atomic>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>

namespace vehicle {

// 共享内存无锁 SPSC 环形缓冲区
// 单生产者单消费者，用于进程间高速数据传输
// 消息格式: [uint32_t payloadLen][payloadLen bytes of data]
class ShmRingBuffer {
public:
    // 创建（由 supervisor 调用）
    static ShmRingBuffer* create(const std::string& name, size_t capacity) {
        size_t totalSize = sizeof(Header) + capacity;
        int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) throw std::runtime_error("shm_open create failed: " + name);
        if (ftruncate(fd, totalSize) < 0) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("ftruncate failed");
        }
        void* ptr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (ptr == MAP_FAILED) {
            shm_unlink(name.c_str());
            throw std::runtime_error("mmap failed");
        }

        auto* hdr = new (ptr) Header();
        hdr->capacity  = capacity;
        hdr->writePos.store(0, std::memory_order_relaxed);
        hdr->readPos.store(0, std::memory_order_relaxed);

        auto* rb = new ShmRingBuffer();
        rb->header_   = hdr;
        rb->data_     = static_cast<char*>(ptr) + sizeof(Header);
        rb->shmName_  = name;
        rb->totalSize_= totalSize;
        rb->owner_    = true;
        return rb;
    }

    // 打开已存在的（由子进程调用）
    static ShmRingBuffer* open(const std::string& name) {
        int fd = shm_open(name.c_str(), O_RDWR, 0666);
        if (fd < 0) throw std::runtime_error("shm_open open failed: " + name);

        // 先读 header 获取 capacity
        Header tmp{};
        if (::read(fd, &tmp, sizeof(Header)) != sizeof(Header)) {
            close(fd);
            throw std::runtime_error("read header failed");
        }
        size_t totalSize = sizeof(Header) + tmp.capacity;
        lseek(fd, 0, SEEK_SET);

        void* ptr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (ptr == MAP_FAILED) throw std::runtime_error("mmap failed");

        auto* rb = new ShmRingBuffer();
        rb->header_   = static_cast<Header*>(ptr);
        rb->data_     = static_cast<char*>(ptr) + sizeof(Header);
        rb->shmName_  = name;
        rb->totalSize_= totalSize;
        rb->owner_    = false;
        return rb;
    }

    ~ShmRingBuffer() {
        if (header_) {
            munmap(header_, totalSize_);
        }
        if (owner_ && !shmName_.empty()) {
            shm_unlink(shmName_.c_str());
        }
    }

    // 生产者：写入一条消息
    // 返回 true 成功，false 缓冲区满
    bool write(const void* data, uint32_t len) {
        uint32_t totalLen = sizeof(uint32_t) + len;  // header + payload
        uint32_t cap = header_->capacity;
        uint32_t w = header_->writePos.load(std::memory_order_relaxed);
        uint32_t r = header_->readPos.load(std::memory_order_acquire);

        // 计算可用空间
        uint32_t used = (w >= r) ? (w - r) : (cap - r + w);
        if (cap - used < totalLen + 1) return false;  // 空间不足

        // 写长度头
        uint32_t netLen = len;  // 直接存储，同机通信不需要字节序转换
        writeRaw(w, &netLen, sizeof(uint32_t));
        w = (w + sizeof(uint32_t)) % cap;

        // 写载荷
        uint32_t first = std::min(len, cap - w);
        memcpy(data_ + w, data, first);
        if (first < len) {
            memcpy(data_, static_cast<const char*>(data) + first, len - first);
        }
        w = (w + len) % cap;

        header_->writePos.store(w, std::memory_order_release);
        return true;
    }

    // 消费者：读取一条消息
    // 返回读取的字节数，0 表示空
    uint32_t read(void* buf, uint32_t bufSize) {
        uint32_t cap = header_->capacity;
        uint32_t w = header_->writePos.load(std::memory_order_acquire);
        uint32_t r = header_->readPos.load(std::memory_order_relaxed);

        if (r == w) return 0;  // 空

        // 读长度头
        uint32_t payloadLen;
        readRaw(r, &payloadLen, sizeof(uint32_t));
        r = (r + sizeof(uint32_t)) % cap;

        if (payloadLen > bufSize) return 0;  // 缓冲区不够

        // 读载荷
        uint32_t first = std::min(payloadLen, cap - r);
        memcpy(buf, data_ + r, first);
        if (first < payloadLen) {
            memcpy(static_cast<char*>(buf) + first, data_, payloadLen - first);
        }
        r = (r + payloadLen) % cap;

        header_->readPos.store(r, std::memory_order_release);
        return payloadLen;
    }

    // 查询当前已用字节数
    size_t usedBytes() const {
        uint32_t w = header_->writePos.load(std::memory_order_acquire);
        uint32_t r = header_->readPos.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (header_->capacity - r + w);
    }

    bool empty() const { return usedBytes() == 0; }

private:
    ShmRingBuffer() = default;
    ShmRingBuffer(const ShmRingBuffer&) = delete;
    ShmRingBuffer& operator=(const ShmRingBuffer&) = delete;

    void writeRaw(uint32_t pos, const void* src, uint32_t len) {
        uint32_t cap = header_->capacity;
        uint32_t first = std::min(len, cap - pos);
        memcpy(data_ + pos, src, first);
        if (first < len) {
            memcpy(data_, static_cast<const char*>(src) + first, len - first);
        }
    }

    void readRaw(uint32_t pos, void* dst, uint32_t len) const {
        uint32_t cap = header_->capacity;
        uint32_t first = std::min(len, cap - pos);
        memcpy(dst, data_ + pos, first);
        if (first < len) {
            memcpy(static_cast<char*>(dst) + first, data_, len - first);
        }
    }

    struct Header {
        std::atomic<uint32_t> writePos{0};
        std::atomic<uint32_t> readPos{0};
        uint32_t capacity{0};
        uint32_t reserved{0};
    };

    Header*    header_    = nullptr;
    char*      data_      = nullptr;
    std::string shmName_;
    size_t     totalSize_ = 0;
    bool       owner_     = false;
};

} // namespace vehicle
