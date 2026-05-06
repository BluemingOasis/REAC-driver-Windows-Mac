#include "macos_pcap_capture.h"

#include <pcap/pcap.h>

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

bool parse_one_based_index(const std::string& value, size_t max, size_t& out)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > max) {
        return false;
    }
    out = static_cast<size_t>(parsed - 1);
    return true;
}

} // namespace

MacPcapCapture::MacPcapCapture() = default;

MacPcapCapture::~MacPcapCapture()
{
    close();
}

const std::string& MacPcapCapture::last_error() const
{
    return last_error_;
}

std::vector<MacPcapDevice> MacPcapCapture::list_devices()
{
    std::vector<MacPcapDevice> devices;
    char errbuf[PCAP_ERRBUF_SIZE]{};
    pcap_if_t* all_devices = nullptr;
    if (pcap_findalldevs(&all_devices, errbuf) != 0) {
        last_error_ = errbuf;
        return devices;
    }

    for (pcap_if_t* dev = all_devices; dev; dev = dev->next) {
        if (!dev->name) {
            continue;
        }
        MacPcapDevice info;
        info.name = dev->name;
        info.description = dev->description ? dev->description : dev->name;
        devices.push_back(info);
    }

    pcap_freealldevs(all_devices);
    return devices;
}

std::string MacPcapCapture::resolve_device_name(const std::string& selector, const std::vector<MacPcapDevice>& devices) const
{
    size_t index = 0;
    if (parse_one_based_index(selector, devices.size(), index)) {
        return devices[index].name;
    }

    for (const MacPcapDevice& device : devices) {
        if (device.name == selector) {
            return device.name;
        }
    }

    const std::string needle = lowercase(selector);
    for (const MacPcapDevice& device : devices) {
        if (lowercase(device.name).find(needle) != std::string::npos ||
            lowercase(device.description).find(needle) != std::string::npos) {
            return device.name;
        }
    }

    return {};
}

bool MacPcapCapture::open(const std::string& device_name)
{
    close();

    char errbuf[PCAP_ERRBUF_SIZE]{};
    pcap_t* handle = pcap_create(device_name.c_str(), errbuf);
    if (!handle) {
        last_error_ = errbuf;
        return false;
    }

    pcap_set_snaplen(handle, 2048);
    pcap_set_promisc(handle, 1);
    pcap_set_timeout(handle, 10);
    pcap_set_buffer_size(handle, 4 * 256 * 1024);

    const int activate_result = pcap_activate(handle);
    if (activate_result != 0) {
        last_error_ = pcap_geterr(handle);
        pcap_close(handle);
        return false;
    }

    bpf_program filter{};
    if (pcap_compile(handle, &filter, "ether proto 0x8819", 1, PCAP_NETMASK_UNKNOWN) != 0) {
        last_error_ = pcap_geterr(handle);
        pcap_close(handle);
        return false;
    }

    const int filter_result = pcap_setfilter(handle, &filter);
    pcap_freecode(&filter);
    if (filter_result != 0) {
        last_error_ = pcap_geterr(handle);
        pcap_close(handle);
        return false;
    }

    handle_ = handle;
    return true;
}

void MacPcapCapture::capture_loop(const PacketCallback& callback, int max_seconds)
{
    pcap_t* handle = static_cast<pcap_t*>(handle_);
    if (!handle) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (max_seconds > 0 && std::chrono::steady_clock::now() - started >= std::chrono::seconds(max_seconds)) {
            return;
        }

        pcap_pkthdr* header = nullptr;
        const u_char* payload = nullptr;
        const int result = pcap_next_ex(handle, &header, &payload);
        if (result == 1 && header && payload) {
            if (!callback(payload, header->caplen)) {
                return;
            }
        } else if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else if (result < 0) {
            last_error_ = pcap_geterr(handle);
            return;
        }
    }
}

void MacPcapCapture::close()
{
    if (handle_) {
        pcap_close(static_cast<pcap_t*>(handle_));
        handle_ = nullptr;
    }
}
