#include "pcap_capture.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

constexpr int kPcapErrbufSize = 256;

struct pcap_t;
struct pcap_addr_t;
struct bpf_insn;

using bpf_u_int32 = uint32_t;

struct pcap_if_t {
    pcap_if_t* next;
    char* name;
    char* description;
    pcap_addr_t* addresses;
    uint32_t flags;
};

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    pcap_timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

struct bpf_program {
    uint32_t bf_len;
    bpf_insn* bf_insns;
};

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

struct PcapCapture::Impl {
#ifdef _WIN32
    using pcap_findalldevs_fn = int (*)(pcap_if_t**, char*);
    using pcap_freealldevs_fn = void (*)(pcap_if_t*);
    using pcap_create_fn = pcap_t* (*)(const char*, char*);
    using pcap_set_snaplen_fn = int (*)(pcap_t*, int);
    using pcap_set_promisc_fn = int (*)(pcap_t*, int);
    using pcap_set_timeout_fn = int (*)(pcap_t*, int);
    using pcap_set_buffer_size_fn = int (*)(pcap_t*, int);
    using pcap_activate_fn = int (*)(pcap_t*);
    using pcap_compile_fn = int (*)(pcap_t*, bpf_program*, const char*, int, bpf_u_int32);
    using pcap_setfilter_fn = int (*)(pcap_t*, bpf_program*);
    using pcap_freecode_fn = void (*)(bpf_program*);
    using pcap_geterr_fn = char* (*)(pcap_t*);
    using pcap_next_ex_fn = int (*)(pcap_t*, pcap_pkthdr**, const uint8_t**);
    using pcap_close_fn = void (*)(pcap_t*);
    using pcap_getevent_fn = HANDLE (*)(pcap_t*);

    HMODULE library = nullptr;
    pcap_t* capture = nullptr;

    pcap_findalldevs_fn pcap_findalldevs = nullptr;
    pcap_freealldevs_fn pcap_freealldevs = nullptr;
    pcap_create_fn pcap_create = nullptr;
    pcap_set_snaplen_fn pcap_set_snaplen = nullptr;
    pcap_set_promisc_fn pcap_set_promisc = nullptr;
    pcap_set_timeout_fn pcap_set_timeout = nullptr;
    pcap_set_buffer_size_fn pcap_set_buffer_size = nullptr;
    pcap_activate_fn pcap_activate = nullptr;
    pcap_compile_fn pcap_compile = nullptr;
    pcap_setfilter_fn pcap_setfilter = nullptr;
    pcap_freecode_fn pcap_freecode = nullptr;
    pcap_geterr_fn pcap_geterr = nullptr;
    pcap_next_ex_fn pcap_next_ex = nullptr;
    pcap_close_fn pcap_close = nullptr;
    pcap_getevent_fn pcap_getevent = nullptr;
#endif

    std::string error;

    template <typename Fn>
    bool load_fn(Fn& fn, const char* name)
    {
#ifdef _WIN32
        fn = reinterpret_cast<Fn>(GetProcAddress(library, name));
        if (!fn) {
            error = std::string("Npcap is missing required function: ") + name;
            return false;
        }
        return true;
#else
        (void)fn;
        (void)name;
        error = "Npcap capture is only implemented on Windows in this example.";
        return false;
#endif
    }

    bool load()
    {
#ifdef _WIN32
        library = LoadLibraryA("wpcap.dll");
        if (!library) {
            error = "Could not load wpcap.dll. Install Npcap, then try again.";
            return false;
        }

        return load_fn(pcap_findalldevs, "pcap_findalldevs") &&
               load_fn(pcap_freealldevs, "pcap_freealldevs") &&
               load_fn(pcap_create, "pcap_create") &&
               load_fn(pcap_set_snaplen, "pcap_set_snaplen") &&
               load_fn(pcap_set_promisc, "pcap_set_promisc") &&
               load_fn(pcap_set_timeout, "pcap_set_timeout") &&
               load_fn(pcap_set_buffer_size, "pcap_set_buffer_size") &&
               load_fn(pcap_activate, "pcap_activate") &&
               load_fn(pcap_compile, "pcap_compile") &&
               load_fn(pcap_setfilter, "pcap_setfilter") &&
               load_fn(pcap_freecode, "pcap_freecode") &&
               load_fn(pcap_geterr, "pcap_geterr") &&
               load_fn(pcap_next_ex, "pcap_next_ex") &&
               load_fn(pcap_close, "pcap_close") &&
               load_fn(pcap_getevent, "pcap_getevent");
#else
        error = "Npcap capture is only implemented on Windows in this example.";
        return false;
#endif
    }
};

PcapCapture::PcapCapture()
    : impl_(new Impl())
{
    impl_->load();
}

PcapCapture::~PcapCapture()
{
    close();
#ifdef _WIN32
    if (impl_->library) {
        FreeLibrary(impl_->library);
    }
#endif
    delete impl_;
}

bool PcapCapture::available() const
{
#ifdef _WIN32
    return impl_->library && impl_->pcap_findalldevs && impl_->pcap_create;
#else
    return false;
#endif
}

const std::string& PcapCapture::last_error() const
{
    return impl_->error;
}

std::vector<CaptureDeviceInfo> PcapCapture::list_devices()
{
    std::vector<CaptureDeviceInfo> devices;
    if (!available()) {
        return devices;
    }

#ifdef _WIN32
    pcap_if_t* all_devices = nullptr;
    char errbuf[kPcapErrbufSize]{};
    if (impl_->pcap_findalldevs(&all_devices, errbuf) != 0) {
        impl_->error = errbuf;
        return devices;
    }

    for (pcap_if_t* dev = all_devices; dev; dev = dev->next) {
        CaptureDeviceInfo info;
        info.name = dev->name ? dev->name : "";
        info.description = dev->description ? dev->description : info.name;
        devices.push_back(std::move(info));
    }

    impl_->pcap_freealldevs(all_devices);
#endif
    return devices;
}

std::string PcapCapture::resolve_device_name(const std::string& selector,
                                             const std::vector<CaptureDeviceInfo>& devices) const
{
    size_t index = 0;
    if (parse_one_based_index(selector, devices.size(), index)) {
        return devices[index].name;
    }

    for (const CaptureDeviceInfo& device : devices) {
        if (device.name == selector) {
            return device.name;
        }
    }

    const std::string needle = lowercase(selector);
    for (const CaptureDeviceInfo& device : devices) {
        if (lowercase(device.description).find(needle) != std::string::npos ||
            lowercase(device.name).find(needle) != std::string::npos) {
            return device.name;
        }
    }

    return {};
}

bool PcapCapture::open(const std::string& device_name)
{
    close();
    if (!available()) {
        return false;
    }

#ifdef _WIN32
    char errbuf[kPcapErrbufSize]{};
    impl_->capture = impl_->pcap_create(device_name.c_str(), errbuf);
    if (!impl_->capture) {
        impl_->error = errbuf;
        return false;
    }

    impl_->pcap_set_snaplen(impl_->capture, 2048);
    impl_->pcap_set_promisc(impl_->capture, 1);
    impl_->pcap_set_timeout(impl_->capture, 10);
    impl_->pcap_set_buffer_size(impl_->capture, 4 * 256 * 1024);

    const int activate_result = impl_->pcap_activate(impl_->capture);
    if (activate_result != 0) {
        impl_->error = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }

    bpf_program filter{};
    if (impl_->pcap_compile(impl_->capture, &filter, "ether proto 0x8819", 1, 0xffffffffU) != 0) {
        impl_->error = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }

    const int filter_result = impl_->pcap_setfilter(impl_->capture, &filter);
    impl_->pcap_freecode(&filter);
    if (filter_result != 0) {
        impl_->error = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }
#endif

    return true;
}

void PcapCapture::capture_loop(const PacketCallback& callback, int max_seconds)
{
#ifdef _WIN32
    if (!impl_->capture) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    HANDLE event = impl_->pcap_getevent(impl_->capture);
    while (true) {
        if (max_seconds > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - started;
            if (elapsed >= std::chrono::seconds(max_seconds)) {
                return;
            }
        }

        if (event) {
            WaitForSingleObject(event, 100);
        }

        pcap_pkthdr* header = nullptr;
        const uint8_t* payload = nullptr;
        const int result = impl_->pcap_next_ex(impl_->capture, &header, &payload);
        if (result == 1 && header && payload) {
            if (!callback(payload, header->caplen)) {
                return;
            }
        } else if (result < 0) {
            impl_->error = impl_->pcap_geterr(impl_->capture);
            std::cerr << "Capture stopped: " << impl_->error << "\n";
            return;
        }
    }
#else
    (void)callback;
    (void)max_seconds;
#endif
}

void PcapCapture::close()
{
#ifdef _WIN32
    if (impl_->capture) {
        impl_->pcap_close(impl_->capture);
        impl_->capture = nullptr;
    }
#endif
}
