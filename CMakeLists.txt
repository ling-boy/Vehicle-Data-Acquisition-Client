cmake_minimum_required(VERSION 3.10)

project("VehicleClientProjV2.0")

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pthread -g -O0 -w -Wno-deprecated-declarations")

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# ==============================================================================
# 依赖库
# ==============================================================================
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenCV REQUIRED)
find_package(PCL 1.2 REQUIRED)

find_library(PORTAUDIO_LIB portaudio REQUIRED)
if (NOT PORTAUDIO_LIB)
    message(FATAL_ERROR "PortAudio library not found")
endif()

# ==============================================================================
# 全局包含目录
# ==============================================================================
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${PCL_INCLUDE_DIRS}
    /usr/include/boost
)

# ==============================================================================
# 共享基础设施库 (shared/)
# ==============================================================================
file(GLOB SHARED_SOURCES ${CMAKE_SOURCE_DIR}/shared/SharedQueue.cpp)

add_library(shared_lib STATIC ${SHARED_SOURCES})
target_include_directories(shared_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(shared_lib
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    ${PCL_LIBRARIES}
    pthread
)

# ==============================================================================
# 采集进程 VehicleCollect
# ==============================================================================
file(GLOB DATA_COLLECTION_SOURCES ${CMAKE_SOURCE_DIR}/data_collection/*.cpp)
# 排除旧的 radar_sim.cpp（独立工具，有自己main）
list(FILTER DATA_COLLECTION_SOURCES EXCLUDE REGEX "radar_sim\\.cpp$")

add_executable(VehicleCollect
    ${DATA_COLLECTION_SOURCES}
    data_collection/main.cpp
)
target_include_directories(VehicleCollect PRIVATE
    ${CMAKE_SOURCE_DIR}/data_collection
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(VehicleCollect
    shared_lib
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    ${PCL_LIBRARIES}
    ${PORTAUDIO_LIB}
    pthread
    ssl
    crypto
)

# ==============================================================================
# 处理进程 VehicleProcess
# ==============================================================================
file(GLOB DATA_PROCESS_SOURCES ${CMAKE_SOURCE_DIR}/data_process/*.cpp)

add_executable(VehicleProcess
    ${DATA_PROCESS_SOURCES}
    data_process/main.cpp
)
target_include_directories(VehicleProcess PRIVATE
    ${CMAKE_SOURCE_DIR}/data_process
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(VehicleProcess
    shared_lib
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    ${PCL_LIBRARIES}
    pthread
)

# ==============================================================================
# 传输进程 VehicleTransfer
# ==============================================================================
file(GLOB FILE_TRANSFER_SOURCES ${CMAKE_SOURCE_DIR}/file_transfer/*.cpp)

add_executable(VehicleTransfer
    ${FILE_TRANSFER_SOURCES}
    file_transfer/main.cpp
)
target_include_directories(VehicleTransfer PRIVATE
    ${CMAKE_SOURCE_DIR}/file_transfer
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(VehicleTransfer
    shared_lib
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    ${PCL_LIBRARIES}
    pthread
    ssl
    crypto
)

# ==============================================================================
# Supervisor 主进程 (总入口)
# ==============================================================================
add_executable(VehicleClient
    main.cpp
    shared/SharedQueue.cpp
)
target_include_directories(VehicleClient PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/supervisor
)
target_link_libraries(VehicleClient
    ${Boost_LIBRARIES}
    ${OpenCV_LIBS}
    ${PCL_LIBRARIES}
    pthread
    rt
)

# ==============================================================================
# 雷达模拟工具（独立）
# ==============================================================================
add_executable(RadarSim
    data_collection/radar_sim.cpp
)
target_include_directories(RadarSim PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
)
target_link_libraries(RadarSim
    shared_lib
    ${PCL_LIBRARIES}
    pthread
)
