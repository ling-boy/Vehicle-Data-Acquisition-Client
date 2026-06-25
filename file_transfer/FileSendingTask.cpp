#include "FileSendingTask.h"
#include "../shared/SharedQueue.h"
#include "../shared/SignalHandler.h"
#include "../shared/Logger.h"
#include <iostream>
#include <thread>

void fileSendingTask(const std::string &server, int port)
{
    if (!g_transferRing) {
        LOG_ERROR("g_transferRing is null, cannot read from shared memory");
        return;
    }

    auto file_send = FileSender::createNew(server, port);
    LOG_INFO("FileSendingTask started, reading from shared memory ring buffer");

    while (!vehicle::SignalHandler::instance().shouldExit())
    {
        // 从共享内存读取目录路径
        char buf[256] = {0};
        uint32_t bytesRead = g_transferRing->read(buf, sizeof(buf));

        if (bytesRead == 0) {
            // 共享内存为空，等待一小段时间再试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string directoryToSend(buf);
        int retryCount = 0;

        // 发送，带重试
        bool success = false;
        while (!success && retryCount < vehicle::DEFAULT_MAX_RETRIES &&
               !vehicle::SignalHandler::instance().shouldExit())
        {
            LOG_INFO("Sending files in directory: " << directoryToSend
                     << " (Attempt " << (retryCount + 1) << "/" << vehicle::DEFAULT_MAX_RETRIES << ")");
            success = file_send->start(directoryToSend);

            if (!success)
            {
                LOG_ERROR("Failed to send directory: " << directoryToSend);
                retryCount++;
                if (retryCount < vehicle::DEFAULT_MAX_RETRIES)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(vehicle::DEFAULT_RETRY_DELAY_MS));
                }
            }
        }

        if (!success && retryCount == vehicle::DEFAULT_MAX_RETRIES)
        {
            LOG_ERROR("Failed to send directory: " << directoryToSend
                      << " after " << vehicle::DEFAULT_MAX_RETRIES << " attempts, skipping");
        }
    }

    LOG_INFO("FileSendingTask stopped");
}
