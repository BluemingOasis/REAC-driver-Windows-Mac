#include "macos_pcap_capture.h"
#include "reac_decoder.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(const char* exe)
{
    std::cerr << "Usage: " << exe << " --list-devices\n"
              << "       " << exe << " --device <index|name|text> [--seconds N]\n";
}

bool parse_positive_int(const char* value, int& out)
{
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    bool list_devices = false;
    std::string device_selector;
    int seconds = 10;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list-devices") {
            list_devices = true;
        } else if ((arg == "--device" || arg == "-d") && i + 1 < argc) {
            device_selector = argv[++i];
        } else if ((arg == "--seconds" || arg == "-s") && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], seconds)) {
                usage(argv[0]);
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    MacPcapCapture capture;
    const std::vector<MacPcapDevice> devices = capture.list_devices();
    if (list_devices) {
        for (size_t i = 0; i < devices.size(); ++i) {
            std::cerr << (i + 1) << ": " << devices[i].description << "\n"
                      << "   " << devices[i].name << "\n";
        }
        return 0;
    }

    if (device_selector.empty()) {
        usage(argv[0]);
        return 2;
    }

    const std::string device_name = capture.resolve_device_name(device_selector, devices);
    if (device_name.empty()) {
        std::cerr << "Could not resolve device '" << device_selector << "'. Use --list-devices.\n";
        return 1;
    }

    if (!capture.open(device_name)) {
        std::cerr << "Could not open " << device_name << ": " << capture.last_error() << "\n";
        std::cerr << "You may need to run with sudo or grant packet-capture permissions.\n";
        return 1;
    }

    reac::Decoder decoder;
    reac::DecodedMultichannelPacket packet;
    uint64_t decoded = 0;
    uint64_t skipped = 0;
    uint64_t counter_misses = 0;
    uint16_t last_counter = 0;
    bool have_counter = false;

    std::cerr << "Listening on " << device_name << " for " << seconds << " seconds...\n";
    capture.capture_loop([&](const uint8_t* data, uint32_t size) {
        const reac::DecodeStatus status = decoder.decode_all(data, size, packet);
        if (status != reac::DecodeStatus::ok) {
            ++skipped;
            return true;
        }

        if (have_counter && static_cast<uint16_t>(last_counter + 1) != packet.counter) {
            ++counter_misses;
        }
        last_counter = packet.counter;
        have_counter = true;
        ++decoded;

        if (decoded % 4000 == 0) {
            std::cerr << "Decoded " << decoded << " packets\n";
        }
        return true;
    }, seconds);

    std::cerr << "Done. decoded=" << decoded
              << " skipped=" << skipped
              << " counter_misses=" << counter_misses << "\n";

    return decoded > 0 ? 0 : 1;
}
