#include "ProcessingTask.h"
#include "../shared/SignalHandler.h"
#include "../shared/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

void ProcessingTask()
{
    auto data_processing = DataProcessing::createNew();
    if (!data_processing->processDirectories())
    {
        LOG_ERROR("DataProcessing failed");
        return;
    }

    // 等待停止信号
    while (!vehicle::SignalHandler::instance().shouldExit())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    LOG_INFO("ProcessingTask received stop signal");
}
