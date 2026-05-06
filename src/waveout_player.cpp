#include "waveout_player.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <thread>

namespace {

std::string lowercase(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool parse_one_based_index(const std::string& value, size_t max, uint32_t& out)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > max) {
        return false;
    }
    out = static_cast<uint32_t>(parsed - 1);
    return true;
}

} // namespace

WaveOutPlayer::WaveOutPlayer() = default;

WaveOutPlayer::~WaveOutPlayer()
{
    close();
}

std::vector<WaveOutPlayer::DeviceInfo> WaveOutPlayer::list_devices()
{
    std::vector<DeviceInfo> devices;
#ifdef _WIN32
    const UINT count = waveOutGetNumDevs();
    devices.reserve(count);
    for (UINT id = 0; id < count; ++id) {
        WAVEOUTCAPSA caps{};
        if (waveOutGetDevCapsA(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            devices.push_back(DeviceInfo{id, caps.szPname});
        }
    }
#endif
    return devices;
}

bool WaveOutPlayer::resolve_device_id(const std::string& selector, uint32_t& id)
{
    const std::vector<DeviceInfo> devices = list_devices();
    if (parse_one_based_index(selector, devices.size(), id)) {
        return true;
    }

    for (const DeviceInfo& device : devices) {
        if (device.name == selector) {
            id = device.id;
            return true;
        }
    }

    const std::string needle = lowercase(selector);
    for (const DeviceInfo& device : devices) {
        if (lowercase(device.name).find(needle) != std::string::npos) {
            id = device.id;
            return true;
        }
    }

    return false;
}

bool WaveOutPlayer::open(int sample_rate, int channels, uint32_t device_id)
{
#ifndef _WIN32
    (void)sample_rate;
    (void)channels;
    (void)device_id;
    return false;
#else
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channels);
    format.nSamplesPerSec = static_cast<DWORD>(sample_rate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const UINT winmm_device_id = device_id == 0xffffffffU ? WAVE_MAPPER : static_cast<UINT>(device_id);
    return waveOutOpen(&device_, winmm_device_id, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
#endif
}

bool WaveOutPlayer::write(const float* interleaved_samples, int frame_count)
{
#ifndef _WIN32
    (void)interleaved_samples;
    (void)frame_count;
    return false;
#else
    if (!device_) {
        return false;
    }

    Buffer& buffer = buffers_[next_buffer_];
    while (buffer.header.dwFlags & WHDR_INQUEUE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (buffer.header.dwFlags & WHDR_PREPARED) {
        waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
    }

    const int sample_count = frame_count * 2;
    buffer.samples.resize(sample_count);
    for (int i = 0; i < sample_count; ++i) {
        const float clamped = std::clamp(interleaved_samples[i], -1.0f, 0.9999695f);
        buffer.samples[i] = static_cast<int16_t>(clamped * 32768.0f);
    }

    buffer.header = {};
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(int16_t));

    if (waveOutPrepareHeader(device_, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
        return false;
    }

    if (waveOutWrite(device_, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
        return false;
    }

    next_buffer_ = (next_buffer_ + 1) % kBufferCount;
    return true;
#endif
}

void WaveOutPlayer::close()
{
#ifdef _WIN32
    if (!device_) {
        return;
    }

    waveOutReset(device_);
    for (Buffer& buffer : buffers_) {
        if (buffer.header.dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(device_, &buffer.header, sizeof(buffer.header));
        }
    }
    waveOutClose(device_);
    device_ = nullptr;
#endif
}
