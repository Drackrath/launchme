#pragma once
#include "../model/devices.hpp"
#include <filesystem>
#include <memory>

class DeviceService {
public:
    virtual ~DeviceService() = default;
    virtual void open(devices::Panel panel) = 0;
    virtual void close() = 0;
    virtual void submit(devices::Command command) = 0;
    virtual devices::Snapshot snapshot() = 0; // images may be a consumed upload delta
};
std::unique_ptr<DeviceService> windowsDeviceService();
std::vector<uint8_t> controllerPicture(devices::Picture kind, const std::filesystem::path& cache);
std::vector<uint8_t> headphonePicture(const std::filesystem::path& cache);
