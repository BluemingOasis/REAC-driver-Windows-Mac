#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

class WaveOutPlayer {
public:
    struct DeviceInfo {
        uint32_t id = 0;
        std::string name;
    };

    WaveOutPlayer();
    ~WaveOutPlayer();

    WaveOutPlayer(const WaveOutPlayer&) = delete;
    WaveOutPlayer& operator=(const WaveOutPlayer&) = delete;

    static std::vector<DeviceInfo> list_devices();
    static bool resolve_device_id(const std::string& selector, uint32_t& id);

    bool open(int sample_rate, int channels, uint32_t device_id = 0xffffffffU);
    bool write(const float* interleaved_samples, int frame_count);
    void close();

private:
#ifdef _WIN32
    struct Buffer {
        WAVEHDR header{};
        std::vector<int16_t> samples;
    };

    static constexpr int kBufferCount = 16;
    std::array<Buffer, kBufferCount> buffers_{};
    HWAVEOUT device_ = nullptr;
    int next_buffer_ = 0;
#endif
};
