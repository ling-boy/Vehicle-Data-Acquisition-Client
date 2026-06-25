#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <memory>
#include <cassert>
#include <new>

namespace vehicle {

// 固定大小对象内存池 (Slab Allocator)
// 线程安全，适用于高频创建/销毁相同大小对象的场景
template <typename T, size_t BlockSize = 4096>
class MemoryPool {
public:
    static constexpr size_t SLOT_SIZE = sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*);

    explicit MemoryPool(size_t initialSlots = 256) {
        grow(initialSlots);
    }

    ~MemoryPool() {
        for (auto* block : blocks_) {
            ::operator delete(block);
        }
    }

    // 禁止拷贝
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 允许移动
    MemoryPool(MemoryPool&& other) noexcept
        : blocks_(std::move(other.blocks_))
        , freeList_(other.freeList_)
        , totalSlots_(other.totalSlots_)
        , freeSlots_(other.freeSlots_) {
        other.freeList_ = nullptr;
        other.totalSlots_ = 0;
        other.freeSlots_ = 0;
    }

    // 获取一个对象槽位（返回未初始化内存）
    T* acquire() {
        std::lock_guard<std::mutex> lk(mu_);
        if (!freeList_) {
            grow(totalSlots_);  // 容量翻倍
        }
        Slot* slot = freeList_;
        freeList_ = slot->next;
        --freeSlots_;
        return reinterpret_cast<T*>(slot);
    }

    // 归还一个对象槽位
    void release(T* ptr) {
        if (!ptr) return;
        ptr->~T();  // 显式析构
        std::lock_guard<std::mutex> lk(mu_);
        auto* slot = reinterpret_cast<Slot*>(ptr);
        slot->next = freeList_;
        freeList_ = slot;
        ++freeSlots_;
    }

    // 统计信息
    size_t totalSlots() const { return totalSlots_; }
    size_t freeSlots()  const { return freeSlots_; }

    // RAII包装：自动归还的unique_ptr
    struct Deleter {
        MemoryPool* pool;
        void operator()(T* ptr) { if (pool && ptr) pool->release(ptr); }
    };
    using UniquePtr = std::unique_ptr<T, Deleter>;

    UniquePtr acquireUnique() {
        T* p = acquire();
        return UniquePtr(p, Deleter{this});
    }

private:
    union Slot {
        char data[SLOT_SIZE];
        Slot* next;
    };

    void grow(size_t count) {
        size_t bytes = count * SLOT_SIZE;
        auto* block = static_cast<char*>(::operator new(bytes));
        blocks_.push_back(block);

        // 将新块中的每个槽位串入空闲链表
        for (size_t i = 0; i < count; ++i) {
            auto* slot = reinterpret_cast<Slot*>(block + i * SLOT_SIZE);
            slot->next = freeList_;
            freeList_ = slot;
        }
        totalSlots_ += count;
        freeSlots_  += count;
    }

    std::vector<char*> blocks_;
    Slot*              freeList_   = nullptr;
    size_t             totalSlots_ = 0;
    size_t             freeSlots_  = 0;
    std::mutex         mu_;
};

} // namespace vehicle
