#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct CaptureDeviceInfo {
    std::string name;
    std::string description;
};

class PcapCapture {
public:
    using PacketCallback = std::function<bool(const uint8_t* data, uint32_t size)>;

    PcapCapture();
    ~PcapCapture();

    PcapCapture(const PcapCapture&) = delete;
    PcapCapture& operator=(const PcapCapture&) = delete;

    bool available() const;
    const std::string& last_error() const;

    std::vector<CaptureDeviceInfo> list_devices();
    std::string resolve_device_name(const std::string& selector,
                                    const std::vector<CaptureDeviceInfo>& devices) const;

    bool open(const std::string& device_name);
    void capture_loop(const PacketCallback& callback, int max_seconds = 0);
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
