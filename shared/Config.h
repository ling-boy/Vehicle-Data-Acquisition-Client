#pragma once

#include <string>
#include <fstream>
#include <map>
#include <mutex>
#include <algorithm>
#include <iostream>

namespace vehicle {

// 轻量级配置管理器，支持 key=value 和 [section] 分组
class Config {
public:
    static Config& instance() {
        static Config inst;
        return inst;
    }

    bool load(const std::string& filePath) {
        std::lock_guard<std::mutex> lk(mu_);
        std::ifstream f(filePath);
        if (!f.is_open()) {
            std::cerr << "Cannot open config: " << filePath << '\n';
            return false;
        }
        std::string line, section;
        while (std::getline(f, line)) {
            trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                trim(section);
                continue;
            }
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            trim(key); trim(val);
            // 行内注释
            auto cp = val.find_first_of("#;");
            if (cp != std::string::npos) { val = val.substr(0, cp); trim(val); }
            // 去引号
            if (val.size() >= 2 &&
                ((val.front()=='"' && val.back()=='"') ||
                 (val.front()=='\'' && val.back()=='\'')))
                val = val.substr(1, val.size()-2);
            std::string fk = section.empty() ? key : section + "." + key;
            data_[fk] = val;
        }
        return true;
    }

    std::string getString(const std::string& k, const std::string& d="") const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = data_.find(k); return it!=data_.end()?it->second:d;
    }
    int getInt(const std::string& k, int d=0) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = data_.find(k);
        if (it!=data_.end()) try{return std::stoi(it->second);}catch(...){return d;}
        return d;
    }
    bool getBool(const std::string& k, bool d=false) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = data_.find(k);
        if (it!=data_.end()) { std::string v=it->second; std::transform(v.begin(),v.end(),v.begin(),::tolower);
            return v=="true"||v=="1"||v=="yes"; } return d;
    }
    double getDouble(const std::string& k, double d=0.0) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = data_.find(k);
        if (it!=data_.end()) try{return std::stod(it->second);}catch(...){return d;}
        return d;
    }
    void set(const std::string& k, const std::string& v) {
        std::lock_guard<std::mutex> lk(mu_); data_[k]=v;
    }
    bool has(const std::string& k) const {
        std::lock_guard<std::mutex> lk(mu_); return data_.count(k)>0;
    }

private:
    Config() = default;
    static void trim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){return !std::isspace(c);}));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){return !std::isspace(c);}).base(), s.end());
    }
    std::map<std::string,std::string> data_;
    mutable std::mutex mu_;
};

} // namespace vehicle
