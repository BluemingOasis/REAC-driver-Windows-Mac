#include "pcap_capture.h"
#include "reac_decoder.h"
#include "waveout_player.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <cstdint>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* exe)
{
    std::cerr << "Usage: " << exe << " [--device <index|name|text>] [--output <index|name|text>] "
              << "[--left 1..40] [--right 1..40] [--seconds N]\n"
              << "       " << exe << " --list-devices\n"
              << "       " << exe << " --list-audio-devices\n"
              << "Without --device, reads length-prefixed raw Ethernet frames from stdin.\n";
}

bool parse_channel(const char* value, int& channel_zero_based)
{
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > reac::kChannelCount) {
        return false;
    }
    channel_zero_based = static_cast<int>(parsed - 1);
    return true;
}

bool parse_positive_int(const char* value, int& out)
{
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > INT_MAX) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool read_exact(std::istream& in, uint8_t* data, size_t size)
{
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}

class AudioBatcher {
public:
    explicit AudioBatcher(WaveOutPlayer& player)
        : player_(player)
    {
        samples_.reserve(kFramesPerWrite * 2);
    }

    bool push(const reac::DecodedPacket& packet)
    {
        for (int i = 0; i < reac::kSamplesPerPacket; ++i) {
            samples_.push_back(packet.left[i]);
            samples_.push_back(packet.right[i]);
        }

        while (samples_.size() >= kFramesPerWrite * 2) {
            if (!player_.write(samples_.data(), kFramesPerWrite)) {
                return false;
            }
            samples_.erase(samples_.begin(), samples_.begin() + kFramesPerWrite * 2);
        }
        return true;
    }

    bool flush()
    {
        if (samples_.empty()) {
            return true;
        }
        const int frames = static_cast<int>(samples_.size() / 2);
        const bool ok = player_.write(samples_.data(), frames);
        samples_.clear();
        return ok;
    }

private:
    static constexpr size_t kFramesPerWrite = 480;

    WaveOutPlayer& player_;
    std::vector<float> samples_;
};

struct DecodeRuntime {
    reac::Decoder decoder;
    reac::DecodedPacket packet;
    AudioBatcher& batcher;
    int left_channel = 0;
    int right_channel = 1;
    uint64_t decoded_packets = 0;
    uint64_t skipped_packets = 0;
    uint16_t last_counter = 0;
    bool have_counter = false;
    bool audio_error = false;
};

bool handle_packet(DecodeRuntime& runtime, const uint8_t* data, size_t size)
{
    const reac::DecodeStatus status =
        runtime.decoder.decode_stereo(data, size, runtime.left_channel, runtime.right_channel, runtime.packet);
    if (status != reac::DecodeStatus::ok) {
        ++runtime.skipped_packets;
        if (runtime.skipped_packets <= 10 || runtime.skipped_packets % 1000 == 0) {
            std::cerr << "Skipping packet: " << reac::to_string(status) << "\n";
        }
        return true;
    }

    if (runtime.have_counter && static_cast<uint16_t>(runtime.last_counter + 1) != runtime.packet.counter) {
        const uint16_t missed =
            static_cast<uint16_t>(runtime.packet.counter - static_cast<uint16_t>(runtime.last_counter + 1));
        std::cerr << "Packet counter jump: missed " << missed << " packets\n";
    }
    runtime.last_counter = runtime.packet.counter;
    runtime.have_counter = true;

    if (!runtime.batcher.push(runtime.packet)) {
        std::cerr << "Failed to submit audio buffer.\n";
        runtime.audio_error = true;
        return false;
    }

    ++runtime.decoded_packets;
    if (runtime.decoded_packets % 4000 == 0) {
        std::cerr << "Decoded " << runtime.decoded_packets << " packets\n";
    }

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    int left_channel = 0;
    int right_channel = 1;
    bool list_devices = false;
    bool list_audio_devices = false;
    std::string device_selector;
    std::string audio_selector;
    int max_seconds = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--left" || arg == "-l") && i + 1 < argc) {
            if (!parse_channel(argv[++i], left_channel)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if ((arg == "--right" || arg == "-r") && i + 1 < argc) {
            if (!parse_channel(argv[++i], right_channel)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if ((arg == "--device" || arg == "-d") && i + 1 < argc) {
            device_selector = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            audio_selector = argv[++i];
        } else if ((arg == "--seconds" || arg == "-s") && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], max_seconds)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (arg == "--list-devices") {
            list_devices = true;
        } else if (arg == "--list-audio-devices") {
            list_audio_devices = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    PcapCapture capture;
    if (list_devices) {
        if (!capture.available()) {
            std::cerr << capture.last_error() << "\n";
            return 1;
        }

        const std::vector<CaptureDeviceInfo> devices = capture.list_devices();
        if (devices.empty()) {
            std::cerr << "No Npcap capture devices found.\n";
            return 1;
        }

        for (size_t i = 0; i < devices.size(); ++i) {
            std::cerr << (i + 1) << ": " << devices[i].description << "\n"
                      << "   " << devices[i].name << "\n";
        }
        return 0;
    }

    if (list_audio_devices) {
        const std::vector<WaveOutPlayer::DeviceInfo> devices = WaveOutPlayer::list_devices();
        if (devices.empty()) {
            std::cerr << "No Windows waveOut audio devices found.\n";
            return 1;
        }

        for (size_t i = 0; i < devices.size(); ++i) {
            std::cerr << (i + 1) << ": " << devices[i].name << "\n";
        }
        return 0;
    }

    uint32_t audio_device_id = 0xffffffffU;
    if (!audio_selector.empty() && !WaveOutPlayer::resolve_device_id(audio_selector, audio_device_id)) {
        std::cerr << "Could not find audio output '" << audio_selector
                  << "'. Run --list-audio-devices to see available devices.\n";
        return 1;
    }

    WaveOutPlayer player;
    if (!player.open(reac::kSampleRate, 2, audio_device_id)) {
        std::cerr << "Failed to open Windows waveOut audio device.\n";
        return 1;
    }

    AudioBatcher batcher(player);
    DecodeRuntime runtime{reac::Decoder{}, reac::DecodedPacket{}, batcher, left_channel, right_channel};
    std::vector<uint8_t> raw;

    if (!device_selector.empty()) {
        if (!capture.available()) {
            std::cerr << capture.last_error() << "\n";
            return 1;
        }

        const std::vector<CaptureDeviceInfo> devices = capture.list_devices();
        const std::string device_name = capture.resolve_device_name(device_selector, devices);
        if (device_name.empty()) {
            std::cerr << "Could not find capture device '" << device_selector
                      << "'. Run --list-devices to see available devices.\n";
            return 1;
        }

        if (!capture.open(device_name)) {
            std::cerr << "Failed to open capture device: " << capture.last_error() << "\n";
            return 1;
        }

        std::cerr << "Listening for REAC packets on " << device_name << "\n"
                  << (max_seconds > 0 ? "Timed capture is enabled.\n" : "Press Ctrl+C to stop.\n");

        capture.capture_loop([&runtime](const uint8_t* data, uint32_t size) {
            return !runtime.audio_error && handle_packet(runtime, data, size);
        }, max_seconds);
    } else {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        while (true) {
            uint8_t len_bytes[2]{};
            if (!read_exact(std::cin, len_bytes, sizeof(len_bytes))) {
                break;
            }

            const uint16_t packet_len = static_cast<uint16_t>(len_bytes[0] | (len_bytes[1] << 8));
            raw.resize(packet_len);
            if (!read_exact(std::cin, raw.data(), raw.size())) {
                std::cerr << "Truncated packet on stdin.\n";
                return 1;
            }

            if (!handle_packet(runtime, raw.data(), raw.size())) {
                return 1;
            }
        }
    }

    if (!batcher.flush()) {
        return 1;
    }

    std::cerr << "Done. Decoded packets: " << runtime.decoded_packets
              << ", skipped packets: " << runtime.skipped_packets << "\n";
    return 0;
}
