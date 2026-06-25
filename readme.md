# 车载数据采集传输系统

基于多进程架构的车载传感器数据采集、处理与传输系统。


## 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Ubuntu 20.04+ |
| 编译器 | GCC 9.1+ |
| CMake | 3.10+ |
| C++ 标准 | C++17 |

### 依赖库

```bash
sudo apt update
sudo apt install -y cmake gcc g++ \
    libboost-all-dev \
    libopencv-dev \
    libpcl-dev \
    portaudio19-dev \
    libssl-dev
```


## 编译

```bash
cd /home/zcl/Client_Car
mkdir -p build && cd build
cmake ..
make -j$(nproc)
cd ..
```

编译产物（位于项目根目录）：

| 可执行文件 | 说明 |
|-----------|------|
| `VehicleClient` | Supervisor 主进程 |
| `VehicleCollect` | 数据采集进程 |
| `VehicleProcess` | 数据处理进程 |
| `VehicleTransfer` | 文件传输进程 |
| `RadarSim` | 雷达模拟工具（独立） |

## 运行

```bash
cd /home/zcl/Client_Car

# 前台运行
./VehicleClient config/vehicle.conf

# 后台运行
nohup ./VehicleClient config/vehicle.conf > /dev/null 2>&1 &
```

### 日志

运行日志自动写入 `logs/` 目录：

```bash
tail -f logs/supervisor.log   # Supervisor 日志
tail -f logs/collect.log      # 采集进程日志
tail -f logs/process.log      # 处理进程日志
tail -f logs/transfer.log     # 传输进程日志
```

### 数据输出

采集的传感器数据保存在 `dataCapture/` 目录：

```
dataCapture/Car0001/
├── Audio/          # 音频 WAV 文件
├── Camera/         # 摄像头 JPEG 图片
└── Imu/            # IMU 数据文本文件
```

## 停止

**前台运行时：** 按 `Ctrl+C`

**后台运行时：**

```bash
# 优雅关停（SIGTERM → 子进程有序退出 → 超时 SIGKILL）
kill -SIGTERM $(pgrep -x VehicleClient)

# 确认已停止
ps aux | grep Vehicle | grep -v grep

# 如有残留，强制清理
pkill -9 -x VehicleClient
pkill -9 -x VehicleCollect
pkill -9 -x VehicleProcess
pkill -9 -x VehicleTransfer
```

## 配置文件

`config/vehicle.conf` 格式：

```ini
[server]
host=tstit.x3322.net
port=12345

[log]
dir=logs
level=INFO

[buffer]
ring_capacity=1024

[collection]
serial_device=/dev/ttyUSB0
camera_enabled=true
audio_enabled=true
imu_enabled=true

[camera]
save_interval_ms=100

[audio]
sample_rate=44100
save_interval_ms=5000

[process]
time_window_ms=200
dataset_dir=Dataset
car_id=Car0001
```

