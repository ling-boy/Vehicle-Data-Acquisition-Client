#include "DataCollector.h"
#include "../shared/SharedQueue.h"
#include "../shared/Logger.h"
#include <chrono>

std::unique_ptr<DataCollector> DataCollector::createNew()
{
    return std::make_unique<DataCollector>();
}

// ���ݲɼ�-���캯��
/*
 * ���ȿ�����֪�豸������
 * ͨ�� m_PDmanager->getDevices() ��ȡ��ǰϵͳ�����豸�������֪�豸�̹߳������У������߳�
 */
DataCollector::DataCollector() : m_PDmanager(std::make_unique<PerceptionDeviceManager>()),
                                 m_threadManager(std::make_unique<CameraThreadManager>(m_PDmanager->getDevices()))
{
}

// ���ݲɼ�-��������
DataCollector::~DataCollector()
{
    std::cout << "DataCollector destroyed" << std::endl;
}

// ���ݲɼ��������к���
bool DataCollector::DataCollectorLoopStart(void)
{
    try
    {
        
        // ͼ��ɼ�
        m_threadManager->start(); // �����߳�
        // ��ӡ��ǰ��ʼ����ϵͳ��֪�豸���߳���Ϣ
        const auto &threadInfoList = m_threadManager->getThreadInfoList();
        if (threadInfoList.empty())
        {
            std::cout << "No device is connected to the system." << std::endl;
        }
        else
        {
            /* ��ӡ��ʼ�����Ѿ����ӵ�ϵͳ���豸�߳����� */
            for (const auto &info : threadInfoList)
            {
                std::cout << "ThreadName: " << info.threadName << "\tThread ID: " << info.threadID << " \tcontrols device: " << info.deviceID << std::endl;
            }
        }

        // �����豸�仯�ص�
        m_PDmanager->setDeviceChangeCallback([this](const std::vector<std::string> &newDevices, const std::vector<std::string> &offDevices)
                                             { this->m_threadManager->onDeviceChange(newDevices, offDevices); });
        /********************************************************************************************************
         * ��ֹ���ˣ����ݲɼ����ֵ� ��֪�豸��� �� ��֪�豸���� �߳��������
         ********************************************************************************************************/

        // // ���߳�ִ�к����߼������ݴ���� totalSeconds ������������Ҫѭ���Ĵ���
        // const int intervalSeconds = 10;                  // ÿ��ѭ����ʱ����Ϊ10��
        // int iterations = totalSeconds / intervalSeconds; // ������Ҫѭ���Ĵ���

        // for (int i = 0; i < iterations; ++i)
        // {
        //     std::cout << "main() running " << (i + 1) * intervalSeconds << " out of " << totalSeconds << " seconds of main code" << std::endl;
        //     std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds)); // ���߳�ÿ10���ӡһ����Ϣ
        // }

        // // ����ʣ���ʱ�䣨����У������ⶪʧ����
        // int remainingSeconds = totalSeconds % intervalSeconds;
        // if (remainingSeconds > 0)
        // {
        //     std::cout << "main() running the remaining " << remainingSeconds << " seconds of main code" << std::endl;
        //     std::this_thread::sleep_for(std::chrono::seconds(remainingSeconds));
        // }
        // std::cout << "Data collection completed successfully." << std::endl;
        
        // ��Ƶ�ɼ�
        // 桥接线程：从本地队列读取 SensorData，写入共享内存环形缓冲区
        std::thread bridgeThread([]() {
            LOG_INFO("Bridge thread started: local queue -> shared memory");
            while (cKeepRunning) {
                SensorData data;
                {
                    std::unique_lock<std::mutex> lock(captureToProcessingQueueMutex);
                    captureToProcessingQueueCondition.wait_for(lock, std::chrono::milliseconds(100),
                        [] { return !captureToProcessingQueue.empty() || !cKeepRunning; });
                    if (captureToProcessingQueue.empty()) continue;
                    data = captureToProcessingQueue.front();
                    captureToProcessingQueue.pop();
                }
                if (g_collectRing) {
                    g_collectRing->write(&data, sizeof(SensorData));
                }
            }
            LOG_INFO("Bridge thread stopped");
        });

        AudioCapture audioCapture(44100, 512, 1, 200); // ���ò����ʡ�ÿ����֡���������������� ������Ƶ����
        std::thread audioThread(&AudioCapture::start, &audioCapture);
        Imu imu;
        std::thread imuThread(&Imu::activate, &imu);

        imuThread.join();
        // �ں�������֮ǰ���ȴ���Ƶ�ɼ��߳����
        audioThread.join();
        bridgeThread.join();
        return true; // ����ִ����ɣ�����true
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception caught in DataCollector::start: " << e.what() << std::endl;
        return false; // �����쳣������false
    }
}
