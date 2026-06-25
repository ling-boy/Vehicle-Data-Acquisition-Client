#pragma once

#include <string>
#include <vector>
#include <map>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>

#include "../shared/Logger.h"

namespace vehicle {

// 子进程信息
struct ChildProcess {
    std::string name;
    std::string executable;
    pid_t       pid = -1;
    bool        running = false;
    int         restartCount = 0;
    int         maxRestarts = 5;
    std::chrono::steady_clock::time_point lastStart;
};

class ProcessManager {
public:
    ProcessManager() = default;
    ~ProcessManager() { shutdown(); }

    // 注册子进程
    void registerProcess(const std::string& name, const std::string& executable,
                         int maxRestarts = 5) {
        ChildProcess cp;
        cp.name = name;
        cp.executable = executable;
        cp.maxRestarts = maxRestarts;
        processes_[name] = cp;
    }

    // 启动所有子进程
    bool startAll() {
        for (auto& [name, cp] : processes_) {
            if (!startProcess(cp)) {
                LOG_ERROR("Failed to start process: " << name);
                return false;
            }
        }
        return true;
    }

    // 启动单个子进程
    bool startProcess(ChildProcess& cp) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork() failed for " << cp.name << ": " << strerror(errno));
            return false;
        }

        if (pid == 0) {
            // 子进程：exec 新程序
            // 关闭信号文件描述符等继承的fd
            execl(cp.executable.c_str(), cp.executable.c_str(), nullptr);
            // exec 失败
            _exit(127);
        }

        // 父进程
        cp.pid = pid;
        cp.running = true;
        cp.lastStart = std::chrono::steady_clock::now();
        LOG_INFO("Started process '" << cp.name << "' pid=" << pid);
        return true;
    }

    // 监控子进程（非阻塞），处理崩溃重启
    // 返回 false 表示所有进程已退出
    bool monitorOnce(int timeoutMs = 1000) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid <= 0) {
            // 没有子进程退出
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
            return true;
        }

        // 找到退出的子进程
        for (auto& [name, cp] : processes_) {
            if (cp.pid == pid) {
                cp.running = false;

                if (WIFEXITED(status)) {
                    int code = WEXITSTATUS(status);
                    LOG_WARN("Process '" << name << "' exited with code " << code);
                } else if (WIFSIGNALED(status)) {
                    LOG_WARN("Process '" << name << "' killed by signal " << WTERMSIG(status));
                }

                // 自动重启
                if (shouldRestart_ && cp.restartCount < cp.maxRestarts) {
                    cp.restartCount++;
                    LOG_INFO("Restarting '" << name << "' (attempt " << cp.restartCount << ")");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    startProcess(cp);
                } else {
                    LOG_ERROR("Process '" << name << "' reached max restarts, not restarting");
                }
                break;
            }
        }
        return true;
    }

    // 发送信号给所有子进程
    void signalAll(int sig) {
        for (auto& [name, cp] : processes_) {
            if (cp.running && cp.pid > 0) {
                kill(cp.pid, sig);
                LOG_INFO("Sent signal " << sig << " to '" << name << "' pid=" << cp.pid);
            }
        }
    }

    // 优雅关停所有子进程
    void shutdown() {
        shouldRestart_ = false;

        // 先发 SIGTERM
        signalAll(SIGTERM);

        // 等待子进程退出，最多3秒
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (hasRunningProcesses() &&
               std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid > 0) {
                for (auto& [name, cp] : processes_) {
                    if (cp.pid == pid) {
                        cp.running = false;
                        LOG_INFO("Process '" << name << "' exited gracefully");
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 还没退出的，发 SIGKILL
        for (auto& [name, cp] : processes_) {
            if (cp.running && cp.pid > 0) {
                LOG_WARN("Force killing '" << name << "' pid=" << cp.pid);
                kill(cp.pid, SIGKILL);
                waitpid(cp.pid, nullptr, 0);
                cp.running = false;
            }
        }
    }

    bool hasRunningProcesses() const {
        for (auto& [name, cp] : processes_) {
            if (cp.running) return true;
        }
        return false;
    }

    void disableAutoRestart() { shouldRestart_ = false; }
    void enableAutoRestart()  { shouldRestart_ = true; }

    const std::map<std::string, ChildProcess>& processes() const { return processes_; }

private:
    std::map<std::string, ChildProcess> processes_;
    bool shouldRestart_ = true;
};

} // namespace vehicle
