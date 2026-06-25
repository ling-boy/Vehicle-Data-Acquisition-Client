#include "DataCollectionTask.h"
#include "../shared/SignalHandler.h"
#include "../shared/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

void dataCollectionTask()
{
    auto data_collector = DataCollector::createNew();
    if (!data_collector->DataCollectorLoopStart())
    {
        LOG_ERROR("DataCollector failed");
        return;
    }

    // 等待停止信号，避免 busy-wait 浪费 CPU
    while (!vehicle::SignalHandler::instance().shouldExit())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    LOG_INFO("DataCollectionTask received stop signal");
}
