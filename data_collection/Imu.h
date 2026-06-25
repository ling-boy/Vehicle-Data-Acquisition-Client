#ifndef IMU_H
#define IMU_H

#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include "../shared/SharedQueue.h"
#include "../shared/SignalHandler.h"

namespace fs = std::filesystem;



class Imu
{
public:
    Imu();
    ~Imu();
    bool activate();

private:
    // ��ȡ���ٶ�����
    std::vector<float> get_acc(const std::vector<uint8_t> &datahex);
    // ��ȡ����������
    std::vector<float> get_gyro(const std::vector<uint8_t> &datahex);
    // ��ȡ�Ƕ�����
    std::vector<float> get_angle(const std::vector<uint8_t> &datahex);
    // �������ݵ��ļ�
    void save_data_to_file(const std::string &folder_path, const std::vector<float> &acc, const std::vector<float> &gyro, const std::vector<float> &angle);
    // �������յ�������
    void GetDataDeal(const std::vector<uint8_t> &list_buf);
    // ��������ĵ��ֽ�����
    void DueData(uint8_t inputdata);
    // �򿪴��ڲ����ò���
    int open_serial(const char *port, int baud_rate);
    std::string getCurrentTime();

    
};

#endif // IMU_H
