#pragma once

#include <atomic>
#include <csignal>
#include <functional>
#include <vector>
#include <mutex>
#include <unistd.h>

namespace vehicle {

class SignalHandler {
public:
    static SignalHandler& instance() {
        static SignalHandler inst;
        return inst;
    }

    // 初始化信号处理
    // 使用 sigaction 注册处理器，子进程调用时会自动解除 fork 继承的阻塞
    bool initialize() {
        // 先解除可能被父进程阻塞的信号
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, SIGTERM);
        sigaddset(&unblock, SIGINT);
        sigprocmask(SIG_UNBLOCK, &unblock, nullptr);

        // 注册信号处理器
        struct sigaction sa{};
        sa.sa_handler = &signalThunk;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;  // 不使用 SA_RESTART，确保 sleep 能被信号中断

        if (sigaction(SIGTERM, &sa, nullptr) < 0) return false;
        if (sigaction(SIGINT,  &sa, nullptr) < 0) return false;

        initialized_ = true;
        return true;
    }

    bool shouldExit() const { return exit_.load(std::memory_order_acquire); }
    void requestExit()      { exit_.store(true, std::memory_order_release); }

    // 阻塞等待退出信号，超时返回 false
    bool waitUntilExit(int timeoutMs = 1000) {
        int remaining = timeoutMs;
        while (remaining > 0 && !shouldExit()) {
            int sleepMs = (remaining < 100) ? remaining : 100;
            usleep(sleepMs * 1000);
            remaining -= sleepMs;
        }
        return shouldExit();
    }

    using Callback = std::function<void(int)>;
    void onSignal(int sig, Callback fn) {
        std::lock_guard<std::mutex> lk(mu_);
        callbacks_.push_back({sig, std::move(fn)});
    }

    ~SignalHandler() = default;

private:
    SignalHandler() = default;

    // 静态信号处理函数（sigaction 回调）
    static void signalThunk(int sig) {
        auto& self = instance();
        if (sig == SIGTERM || sig == SIGINT) {
            self.requestExit();
        }
        std::lock_guard<std::mutex> lk(self.mu_);
        for (auto& cb : self.callbacks_) {
            if (cb.sig == sig) cb.fn(sig);
        }
    }

    struct Entry { int sig; Callback fn; };

    std::atomic<bool> exit_{false};
    bool initialized_ = false;
    std::vector<Entry> callbacks_;
    std::mutex mu_;
};

} // namespace vehicle
