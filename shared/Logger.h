#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <sys/stat.h>

namespace vehicle {

enum class LogLevel : uint8_t {
    DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4
};

inline const char* logLevelStr(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "?????";
    }
}

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    bool init(const std::string& processName,
              const std::string& logDir = "logs",
              LogLevel minLevel = LogLevel::INFO,
              size_t maxFileSize = 10 * 1024 * 1024) {
        std::lock_guard<std::mutex> lk(mu_);
        processName_ = processName;
        minLevel_    = minLevel;
        maxFileSize_ = maxFileSize;
        logDir_      = logDir;
        mkdir(logDir.c_str(), 0755);
        return openFile();
    }

    void setLevel(LogLevel lv) { minLevel_ = lv; }

    void log(LogLevel lv, const char* file, int line, const std::string& msg) {
        if (lv < minLevel_) return;
        std::lock_guard<std::mutex> lk(mu_);
        if (bytes_ >= maxFileSize_) rotate();
        std::string s = fmt(lv, file, line, msg);
        if (ofs_ && ofs_->is_open()) { *ofs_ << s << std::endl; bytes_ += s.size() + 1; }
        if (lv >= LogLevel::ERROR) std::cerr << s << '\n';
    }

    ~Logger() { if (ofs_ && ofs_->is_open()) { ofs_->flush(); ofs_->close(); } }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool openFile() {
        std::string p = logDir_ + "/" + processName_ + ".log";
        ofs_ = std::make_unique<std::ofstream>(p, std::ios::app);
        if (!ofs_->is_open()) { std::cerr << "Cannot open " << p << '\n'; return false; }
        struct stat st{};
        if (stat(p.c_str(), &st) == 0) bytes_ = st.st_size;
        return true;
    }

    void rotate() {
        if (ofs_ && ofs_->is_open()) ofs_->close();
        std::string c = logDir_ + "/" + processName_ + ".log";
        rename(c.c_str(), (c + ".1").c_str());
        openFile();
    }

    std::string fmt(LogLevel lv, const char* file, int line, const std::string& msg) {
        auto now  = std::chrono::system_clock::now();
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        auto t    = std::chrono::system_clock::to_time_t(now);
        std::tm bt{}; localtime_r(&t, &bt);
        std::ostringstream o;
        o << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
          << '.' << std::setfill('0') << std::setw(3) << ms.count()
          << " [" << logLevelStr(lv) << "] [" << processName_ << "] "
          << file << ':' << line << " - " << msg;
        return o.str();
    }

    std::string processName_, logDir_;
    LogLevel minLevel_ = LogLevel::INFO;
    size_t maxFileSize_ = 10*1024*1024, bytes_ = 0;
    std::unique_ptr<std::ofstream> ofs_;
    std::mutex mu_;
};

#define LOG_DEBUG(msg) do { std::ostringstream _o; _o << msg; vehicle::Logger::instance().log(vehicle::LogLevel::DEBUG, __FILE__, __LINE__, _o.str()); } while(0)
#define LOG_INFO(msg)  do { std::ostringstream _o; _o << msg; vehicle::Logger::instance().log(vehicle::LogLevel::INFO,  __FILE__, __LINE__, _o.str()); } while(0)
#define LOG_WARN(msg)  do { std::ostringstream _o; _o << msg; vehicle::Logger::instance().log(vehicle::LogLevel::WARN,  __FILE__, __LINE__, _o.str()); } while(0)
#define LOG_ERROR(msg) do { std::ostringstream _o; _o << msg; vehicle::Logger::instance().log(vehicle::LogLevel::ERROR, __FILE__, __LINE__, _o.str()); } while(0)
#define LOG_FATAL(msg) do { std::ostringstream _o; _o << msg; vehicle::Logger::instance().log(vehicle::LogLevel::FATAL, __FILE__, __LINE__, _o.str()); } while(0)

} // namespace vehicle
