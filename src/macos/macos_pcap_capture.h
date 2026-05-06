#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct MacPcapDevice {
    std::string name;
    std::string description;
};

class MacPcapCapture {
public:
    using PacketCallback = std::function<bool(const uint8_t* data, uint32_t size)>;

    MacPcapCapture();
    ~MacPcapCapture();

    MacPcapCapture(const MacPcapCapture&) = delete;
    MacPcapCapture& operator=(const MacPcapCapture&) = delete;

    const std::string& last_error() const;

    std::vector<MacPcapDevice> list_devices();
    std::string resolve_device_name(const std::string& selector, const std::vector<MacPcapDevice>& devices) const;

    bool open(const std::string& device_name);
    void capture_loop(const PacketCallback& callback, int max_seconds = 0);
    void close();

private:
    void* handle_ = nullptr;
    std::string last_error_;
};
