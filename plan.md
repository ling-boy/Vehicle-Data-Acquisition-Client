# 车载客户端企业级重构方案

## Context

当前项目是一个车载数据采集传输系统，使用多线程+全局变量架构，存在以下核心问题：
- 全局共享队列耦合所有模块，无法独立部署和测试
- 线程生命周期管理混乱（死循环、detach线程、busy-wait）
- 无优雅关停（Ctrl+C直接杀死进程）
- 无日志框架、无配置管理、无内存管理优化
- IMU模块使用文件作用域全局变量，不可重入

目标：重构为**多进程架构**，每个模块独立进程，通过**共享内存+消息队列**通信，加入**内存池+环形缓冲区**、**信号处理**、**日志系统**、**配置管理**等企业级基础设施。

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    Supervisor (main进程)                         │
│  - fork/exec 各子进程                                            │
│  - SIGTERM/SIGINT 信号处理 → 通知所有子进程有序退出                 │
│  - 监控子进程状态，崩溃重启                                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────┐   共享内存环形缓冲区   ┌──────────┐  共享内存  ┌──────────┐
│  │ 采集进程  │ ──────────────────→ │ 处理进程  │ ────────→ │ 传输进程  │
│  │ (Collect)│   + POSIX消息队列     │ (Process)│  环形缓冲  │ (Transfer)│
│  └──────────┘                      └──────────┘           └──────────┘
│      │                                  │                      │
│      ├── CameraThreadManager            ├── 时间窗口分桶         ├── NetworkHandler
│      ├── AudioCapture                   ├── 文件移动/整理        ├── FileSender
│      ├── Imu                            │                      │
│      └── PerceptionDeviceManager        │                      │
│                                         │                      │
├─────────────────────────────────────────────────────────────────┤
│                      共享基础设施 (shared/)                       │
│  ├── ShmRingBuffer    : 共享内存无锁环形缓冲区                     │
│  ├── MemoryPool       : 固定大小对象池                            │
│  ├── MessageQueue     : POSIX消息队列封装 (控制信号)               │
│  ├── Logger           : spdlog风格日志系统                        │
│  ├── Config           : YAML/JSON配置管理                        │
│  ├── SignalHandler    : SIGTERM/SIGINT统一处理                   │
│  └── SensorData       : 数据结构定义                              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 目录结构（重构后）

```
Client_Car/
├── CMakeLists.txt                    # 顶层CMake
├── config/
│   └── config.yaml                   # 统一配置文件
├── shared/                           # 共享基础设施库 (编译为静态库)
│   ├── CMakeLists.txt
│   ├── memory/
│   │   ├── MemoryPool.h/cpp          # 固定大小内存池
│   │   └── ShmRingBuffer.h/cpp       # 共享内存无锁环形缓冲区
│   ├── ipc/
│   │   ├── MessageQueue.h/cpp        # POSIX消息队列封装
│   │   └── ShmManager.h/cpp          # 共享内存创建/映射/销毁
│   ├── log/
│   │   └── Logger.h/cpp              # 日志系统 (基于spdlog或自实现)
│   ├── config/
│   │   └── Config.h/cpp              # YAML配置加载
│   ├── signal/
│   │   └── SignalHandler.h/cpp       # 信号处理
│   ├── data/
│   │   └── SensorData.h              # 数据结构定义
│   └── common/
│       ├── TimeUtil.h/cpp            # 统一时间工具（消除重复代码）
│       └── Types.h                   # 通用类型定义
├── data_collection/                  # 采集进程
│   ├── CMakeLists.txt
│   ├── main.cpp                      # 采集进程入口
│   ├── collector/
│   │   ├── DataCollector.h/cpp       # 采集编排器
│   │   ├── ISensorDriver.h           # 传感器驱动接口（策略模式）
│   │   ├── CameraDriver.h/cpp
│   │   ├── AudioDriver.h/cpp
│   │   ├── ImuDriver.h/cpp
│   │   └── RadarDriver.h/cpp
│   └── device/
│       ├── PerceptionDeviceManager.h/cpp
│       └── CameraThreadManager.h/cpp
├── data_process/                     # 处理进程
│   ├── CMakeLists.txt
│   ├── main.cpp                      # 处理进程入口
│   ├── DataProcessing.h/cpp
│   └── ProcessingTask.h/cpp
├── file_transfer/                    # 传输进程
│   ├── CMakeLists.txt
│   ├── main.cpp                      # 传输进程入口
│   ├── IFileHandler.h
│   ├── INetworkHandler.h
│   ├── FileHandler.h/cpp
│   ├── NetworkHandler.h/cpp
│   ├── FileSender.h/cpp
│   └── FileSendingTask.h/cpp
├── supervisor/                       # Supervisor主进程
│   ├── CMakeLists.txt
│   ├── main.cpp                      # 程序总入口
│   ├── ProcessManager.h/cpp          # 子进程管理 (fork/exec/wait)
│   └── ShutdownCoordinator.h/cpp     # 优雅关停协调器
└── tests/                            # 单元测试
    ├── CMakeLists.txt
    ├── test_memory_pool.cpp
    ├── test_ring_buffer.cpp
    └── test_message_queue.cpp
```

---

## 分阶段实施计划

### Phase 1: 共享基础设施库 (shared/)

**1.1 日志系统 `shared/log/Logger.h`**
- 单例模式，支持多级别 (DEBUG/INFO/WARN/ERROR/FATAL)
- 支持控制台+文件输出，按大小轮转
- 线程安全，使用fmt风格格式化
- 可选：引入spdlog头文件库，或自实现简化版

**1.2 配置管理 `shared/config/Config.h`**
- 单例模式，加载YAML配置文件
- 支持传感器参数、服务器地址、日志级别、缓冲区大小等配置
- 可选：引入yaml-cpp，或使用简单的JSON解析

**1.3 信号处理 `shared/signal/SignalHandler.h`**
- 注册SIGTERM/SIGINT/SIGCHLD处理器
- 使用signalfd或pipe+select避免信号处理器中的async-signal-safety问题
- 提供`shouldExit()`原子查询接口
- 提供`waitForSignal()`阻塞等待接口

**1.4 内存池 `shared/memory/MemoryPool.h`**
- 固定大小块分配器（Slab Allocator）
- 模板化：`MemoryPool<T>`，预分配N个对象
- 线程安全，使用自旋锁或mutex
- 支持`acquire()`/`release()`接口，返回`unique_ptr`带自定义deleter

**1.5 共享内存环形缓冲区 `shared/memory/ShmRingBuffer.h`**
- 基于POSIX共享内存 (`shm_open`/`mmap`)
- 无锁SPSC（单生产者单消费者）环形缓冲区
- 使用内存序（memory_order）保证原子性
- 支持变长消息（头部长+载荷）
- 内部使用MemoryPool管理消息槽位

**1.6 IPC消息队列 `shared/ipc/MessageQueue.h`**
- 基于POSIX消息队列 (`mq_open`)
- 用于传递控制信号（启动、停止、心跳）
- 支持优先级消息
- 封装非阻塞发送/接收

**1.7 共享内存管理器 `shared/ipc/ShmManager.h`**
- 创建/打开/销毁共享内存段
- RAII封装，析构时自动清理
- 支持命名共享内存段（采集→处理、处理→传输）

**1.8 工具类**
- `TimeUtil`：统一`getCurrentTime()`/`getCurrentDateTimeString()`，消除4处重复
- `SensorData`：从SharedQueue.h中独立出来，加入内存池支持

---

### Phase 2: 多进程架构 + Supervisor

**2.1 Supervisor主进程 `supervisor/main.cpp`**
- 程序总入口，替代原main.cpp
- 启动时：创建共享内存段、初始化日志、加载配置
- fork/exec三个子进程（采集、处理、传输）
- 信号处理：收到SIGTERM → 通过消息队列通知各子进程 → waitpid回收
- SIGCHLD处理：子进程崩溃自动重启（带退避策略）

**2.2 子进程入口**
- 每个模块有独立的`main.cpp`，编译为独立可执行文件
- 入口流程：初始化日志 → 注册信号 → 映射共享内存 → 连接环形缓冲区 → 运行主循环
- 收到停止消息后：清理资源 → 取消映射 → 退出

**2.3 CMake改造**
- 顶层CMakeLists.txt使用`add_subdirectory`组织各模块
- shared编译为静态库`libshared.a`
- 各模块链接libshared.a生成独立可执行文件
- supervisor编译为总入口可执行文件

---

### Phase 3: 数据采集模块重构

**3.1 传感器驱动接口（策略模式）**
```cpp
class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;
    virtual bool initialize(const SensorConfig& config) = 0;
    virtual bool startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual std::string name() const = 0;
};
```

**3.2 具体驱动实现**
- `CameraDriver`：封装CameraThreadManager，实现ISensorDriver
- `AudioDriver`：封装AudioCapture，实现ISensorDriver
- `ImuDriver`：封装Imu，消除全局变量，使用成员变量
- `RadarDriver`：集成radar_sim，实现ISensorDriver

**3.3 采集编排器（工厂模式+观察者模式）**
- `DataCollector`持有`vector<unique_ptr<ISensorDriver>>`
- 通过配置文件决定启用哪些驱动（工厂创建）
- 设备热插拔通过观察者模式通知

**3.4 输出改造**
- 采集数据写入共享内存环形缓冲区（替代全局队列）
- 通过消息队列发送元数据通知

---

### Phase 4: 数据处理和传输模块重构

**4.1 处理模块**
- 从环形缓冲区读取SensorData
- 时间窗口分桶逻辑不变
- 处理结果写入第二个环形缓冲区（处理→传输）
- 通过消息队列通知传输模块

**4.2 传输模块**
- 从环形缓冲区读取待传输数据
- 现有接口体系(IFileHandler/INetworkHandler)保留，已较好
- 修复已知bug：socket fd泄漏、send返回值未检查、htonl截断

---

## 关键设计模式汇总

| 模式 | 应用位置 | 目的 |
|------|---------|------|
| **单例** | Logger, Config, SignalHandler | 全局唯一资源管理 |
| **工厂** | ISensorDriver创建, MemoryPool | 解耦创建和使用 |
| **策略** | ISensorDriver接口 | 传感器驱动可插拔 |
| **观察者** | 设备热插拔回调 | 事件驱动通知 |
| **生产者-消费者** | ShmRingBuffer | 高效进程间数据传输 |
| **RAII** | ShmManager, MemoryPool的unique_ptr | 自动资源清理 |
| **模板方法** | 子进程入口流程骨架 | 统一初始化/清理流程 |
| **对象池** | MemoryPool\<SensorData\> | 减少malloc开销和碎片 |

---

## 已知Bug修复（随重构一并修复）

1. **busy-wait CPU浪费** → 改为条件变量/消息队列阻塞等待
2. **detach线程不可join** → 进程模型下不再有此问题
3. **Audio/Imu无shutdown路径** → SignalHandler统一控制
4. **全局变量不可重入** → 改为成员变量
5. **Socket fd泄漏** → NetworkHandler析构函数调用close()
6. **send()返回值未检查** → 补充完整错误检查
7. **htonl截断size_t** → 改用64位网络字节序
8. **重复的getCurrentTime()** → 统一到TimeUtil

---

## 验证方案

1. **编译验证**：`cmake .. && make` 编译通过，生成4个可执行文件
2. **单元测试**：MemoryPool、ShmRingBuffer、MessageQueue的独立测试
3. **集成测试**：启动supervisor，验证3个子进程启动/运行/优雅关停
4. **信号测试**：发送SIGTERM，验证所有进程有序退出，无资源泄漏
5. **崩溃恢复测试**：kill子进程，验证supervisor自动重启

---

## 实施顺序

```
Phase 1 (shared/) → Phase 2 (supervisor + 多进程框架) → Phase 3 (采集重构) → Phase 4 (处理+传输重构)
```

每个Phase完成后的CMake都能独立编译通过，确保增量可用。
