#include "asio_minimal.h"
#include "pcap_capture.h"
#include "reac_decoder.h"
#include "reac_ring_buffer.h"
#include "reac_settings.h"
#include "waveout_player.h"

#include <objbase.h>
#include <shellapi.h>
#include <unknwn.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kDriverName[] = "REAC 40ch ASIO";
constexpr char kAsioRegistryName[] = "REAC 40ch ASIO";
constexpr char kClsidString[] = "{8E2B2FD8-7D31-4A19-8C73-0D6D8285C63B}";
constexpr ASIOLong kInputChannels = 40;
constexpr ASIOLong kOutputChannels = 2;
constexpr ASIOLong kPreferredBufferSize = 512;
constexpr ASIOLong kSampleRate = 48000;
constexpr HRESULT kSelfRegClassError = static_cast<HRESULT>(0x80040153L);

const GUID kDriverClsid = {0x8e2b2fd8, 0x7d31, 0x4a19, {0x8c, 0x73, 0x0d, 0x6d, 0x82, 0x85, 0xc6, 0x3b}};

HMODULE g_module = nullptr;
std::atomic<long> g_object_count{0};
std::atomic<long> g_lock_count{0};

std::string module_path()
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(g_module, path, sizeof(path));
    return path;
}

void log_line(const char* text)
{
    char temp_path[MAX_PATH]{};
    GetTempPathA(sizeof(temp_path), temp_path);
    std::ofstream out(std::string(temp_path) + "reac_asio.log", std::ios::app);
    out << GetTickCount64() << " " << text << "\n";
}

bool write_string_value(HKEY root, const char* path, const char* name, const char* value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>(std::strlen(value) + 1);
    const LONG result = RegSetValueExA(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void delete_tree_if_present(HKEY root, const char* path)
{
    RegDeleteTreeA(root, path);
}

void copy_cstr(char* dest, const char* src, size_t size)
{
    std::snprintf(dest, size, "%s", src);
}

class ReacAsioDriver final : public IASIO {
public:
    ReacAsioDriver()
    {
        log_line("driver ctor");
        ++g_object_count;
    }

    ~ReacAsioDriver()
    {
        log_line("driver dtor");
        stop();
        disposeBuffers();
        --g_object_count;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kDriverClsid)) {
            *out = static_cast<IASIO*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --ref_count_;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }

    ASIOBool init(void*) override
    {
        log_line("init");
        return ASIOTrue;
    }

    void getDriverName(char* name) override
    {
        log_line("getDriverName");
        copy_cstr(name, kDriverName, 32);
    }

    ASIOLong getDriverVersion() override
    {
        log_line("getDriverVersion");
        return 1;
    }

    void getErrorMessage(char* string) override
    {
        log_line("getErrorMessage");
        copy_cstr(string, last_error_.empty() ? "No error" : last_error_.c_str(), 124);
    }

    ASIOError start() override
    {
        log_line("start");
        if (!callbacks_ || input_buffers_.empty()) {
            return ASE_InvalidMode;
        }
        if (running_) {
            return ASE_OK;
        }

        running_ = true;
        open_output_monitor();
        capture_thread_ = std::thread([this] { capture_main(); });
        callback_thread_ = std::thread([this] { callback_main(); });
        return ASE_OK;
    }

    ASIOError stop() override
    {
        log_line("stop");
        if (!running_) {
            return ASE_OK;
        }
        running_ = false;
        capture_.close();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (callback_thread_.joinable()) {
            callback_thread_.join();
        }
        output_player_.close();
        return ASE_OK;
    }

    ASIOError getChannels(ASIOLong* numInputChannels, ASIOLong* numOutputChannels) override
    {
        log_line("getChannels");
        if (!numInputChannels || !numOutputChannels) {
            return ASE_InvalidParameter;
        }
        *numInputChannels = kInputChannels;
        *numOutputChannels = kOutputChannels;
        return ASE_OK;
    }

    ASIOError getLatencies(ASIOLong* inputLatency, ASIOLong* outputLatency) override
    {
        log_line("getLatencies");
        if (!inputLatency || !outputLatency) {
            return ASE_InvalidParameter;
        }
        *inputLatency = buffer_size_;
        *outputLatency = buffer_size_;
        return ASE_OK;
    }

    ASIOError getBufferSize(ASIOLong* minSize, ASIOLong* maxSize, ASIOLong* preferredSize, ASIOLong* granularity) override
    {
        log_line("getBufferSize");
        if (!minSize || !maxSize || !preferredSize || !granularity) {
            return ASE_InvalidParameter;
        }
        *minSize = 128;
        *maxSize = 2048;
        *preferredSize = kPreferredBufferSize;
        *granularity = 0;
        return ASE_OK;
    }

    ASIOError canSampleRate(ASIOSampleRate sampleRate) override
    {
        log_line("canSampleRate");
        return sampleRate == kSampleRate ? ASE_OK : ASE_NoClock;
    }

    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override
    {
        log_line("getSampleRate");
        if (!sampleRate) {
            return ASE_InvalidParameter;
        }
        *sampleRate = kSampleRate;
        return ASE_OK;
    }

    ASIOError setSampleRate(ASIOSampleRate sampleRate) override
    {
        log_line("setSampleRate");
        return canSampleRate(sampleRate);
    }

    ASIOError getClockSources(ASIOClockSource* clocks, ASIOLong* numSources) override
    {
        log_line("getClockSources");
        if (!clocks || !numSources || *numSources < 1) {
            return ASE_InvalidParameter;
        }
        *numSources = 1;
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = ASIOTrue;
        copy_cstr(clocks[0].name, "REAC stream clock", sizeof(clocks[0].name));
        return ASE_OK;
    }

    ASIOError setClockSource(ASIOLong reference) override
    {
        log_line("setClockSource");
        return reference == 0 ? ASE_OK : ASE_InvalidParameter;
    }

    ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override
    {
        log_line("getSamplePosition");
        if (!sPos || !tStamp) {
            return ASE_InvalidParameter;
        }
        const uint64_t pos = sample_position_.load();
        sPos->hi = static_cast<uint32_t>(pos >> 32);
        sPos->lo = static_cast<uint32_t>(pos & 0xffffffffU);

        const uint64_t now = GetTickCount64() * 1000000ULL;
        tStamp->hi = static_cast<uint32_t>(now >> 32);
        tStamp->lo = static_cast<uint32_t>(now & 0xffffffffU);
        return ASE_OK;
    }

    ASIOError getChannelInfo(ASIOChannelInfo* info) override
    {
        log_line("getChannelInfo");
        if (!info) {
            return ASE_InvalidParameter;
        }

        const bool is_input = info->isInput == ASIOTrue;
        const ASIOLong max_channels = is_input ? kInputChannels : kOutputChannels;
        if (info->channel < 0 || info->channel >= max_channels) {
            return ASE_InvalidParameter;
        }

        info->isActive = ASIOTrue;
        info->channelGroup = 0;
        info->type = ASIOSTFloat32LSB;
        if (is_input) {
            std::snprintf(info->name, sizeof(info->name), "REAC %02ld", info->channel + 1);
        } else {
            std::snprintf(info->name, sizeof(info->name), "Silent Out %ld", info->channel + 1);
        }
        return ASE_OK;
    }

    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, ASIOLong numChannels, ASIOLong bufferSize, ASIOCallbacks* callbacks) override
    {
        log_line("createBuffers");
        if (!bufferInfos || !callbacks || bufferSize <= 0) {
            return ASE_InvalidParameter;
        }

        disposeBuffers();
        callbacks_ = callbacks;
        buffer_size_ = bufferSize;
        input_buffers_.resize(kInputChannels);
        output_buffers_.resize(kOutputChannels);
        buffer_ptrs_.resize(kInputChannels);

        for (ASIOLong i = 0; i < numChannels; ++i) {
            ASIOBufferInfo& info = bufferInfos[i];
            const bool is_input = info.isInput == ASIOTrue;
            const ASIOLong max_channels = is_input ? kInputChannels : kOutputChannels;
            if (info.channelNum < 0 || info.channelNum >= max_channels) {
                info.buffers[0] = nullptr;
                info.buffers[1] = nullptr;
                continue;
            }

            auto& channel_buffers = is_input ? input_buffers_[info.channelNum] : output_buffers_[info.channelNum];
            channel_buffers[0].assign(buffer_size_, 0.0f);
            channel_buffers[1].assign(buffer_size_, 0.0f);
            info.buffers[0] = channel_buffers[0].data();
            info.buffers[1] = channel_buffers[1].data();
        }
        return ASE_OK;
    }

    ASIOError disposeBuffers() override
    {
        log_line("disposeBuffers");
        stop();
        input_buffers_.clear();
        output_buffers_.clear();
        buffer_ptrs_.clear();
        callbacks_ = nullptr;
        return ASE_OK;
    }

    ASIOError controlPanel() override
    {
        log_line("controlPanel");
        char config_path[MAX_PATH]{};
        std::snprintf(config_path, sizeof(config_path), "%s", module_path().c_str());
        char* last_slash = std::strrchr(config_path, '\\');
        if (last_slash) {
            *(last_slash + 1) = '\0';
            std::strncat(config_path, "reac_config.exe", sizeof(config_path) - std::strlen(config_path) - 1);
        }

        if (ShellExecuteA(nullptr, "open", config_path, nullptr, nullptr, SW_SHOWNORMAL) <= reinterpret_cast<HINSTANCE>(32)) {
            MessageBoxA(nullptr, "Could not open reac_config.exe.", kDriverName, MB_OK | MB_ICONERROR);
        }
        return ASE_OK;
    }

    ASIOError future(ASIOFuture, void*) override
    {
        log_line("future");
        return ASE_NotPresent;
    }

    ASIOError outputReady() override
    {
        log_line("outputReady");
        return ASE_OK;
    }

private:
    using ChannelDoubleBuffer = std::array<std::vector<float>, 2>;

    void capture_main()
    {
        if (!capture_.available()) {
            last_error_ = capture_.last_error();
            running_ = false;
            return;
        }

        const std::vector<CaptureDeviceInfo> devices = capture_.list_devices();
        const std::string selector = load_reac_settings().capture_selector;
        const std::string device_name = capture_.resolve_device_name(selector, devices);
        if (device_name.empty()) {
            last_error_ = "Could not resolve REAC_ASIO_DEVICE capture adapter";
            running_ = false;
            return;
        }

        if (!capture_.open(device_name)) {
            last_error_ = capture_.last_error();
            running_ = false;
            return;
        }

        reac::Decoder decoder;
        reac::DecodedMultichannelPacket packet;
        capture_.capture_loop([this, &decoder, &packet](const uint8_t* data, uint32_t size) {
            const reac::DecodeStatus status = decoder.decode_all(data, size, packet);
            if (status == reac::DecodeStatus::ok) {
                ring_.push(packet);
            }
            return running_.load();
        });
    }

    void callback_main()
    {
        ASIOTime time{};
        ASIOLong index = 0;
        const auto interval = std::chrono::duration<double>(static_cast<double>(buffer_size_) / kSampleRate);
        auto next = std::chrono::steady_clock::now() + interval;

        while (running_) {
            fill_buffer(index);
            const uint64_t pos = sample_position_.load();
            time.timeInfo.sampleRate = kSampleRate;
            time.timeInfo.samplePosition.hi = static_cast<uint32_t>(pos >> 32);
            time.timeInfo.samplePosition.lo = static_cast<uint32_t>(pos & 0xffffffffU);
            const uint64_t now = GetTickCount64() * 1000000ULL;
            time.timeInfo.systemTime.hi = static_cast<uint32_t>(now >> 32);
            time.timeInfo.systemTime.lo = static_cast<uint32_t>(now & 0xffffffffU);

            if (callbacks_) {
                if (callbacks_->bufferSwitchTimeInfo) {
                    callbacks_->bufferSwitchTimeInfo(&time, index, ASIOFalse);
                } else if (callbacks_->bufferSwitch) {
                    callbacks_->bufferSwitch(index, ASIOFalse);
                }
            }

            write_output_monitor(index);
            sample_position_ += buffer_size_;
            index = index ? 0 : 1;
            std::this_thread::sleep_until(next);
            next += interval;
        }
    }

    void fill_buffer(ASIOLong index)
    {
        if (input_buffers_.size() != kInputChannels || buffer_ptrs_.size() != kInputChannels) {
            return;
        }

        for (ASIOLong channel = 0; channel < kInputChannels; ++channel) {
            buffer_ptrs_[channel] = input_buffers_[channel][index].data();
        }
        ring_.read(buffer_ptrs_.data(), kInputChannels, static_cast<size_t>(buffer_size_));
    }

    void open_output_monitor()
    {
        const std::string selector = load_reac_settings().output_selector;
        uint32_t device_id = 0xffffffffU;
        if (!WaveOutPlayer::resolve_device_id(selector, device_id)) {
            log_line("output monitor: could not resolve output selector, using default");
        }

        if (!output_player_.open(kSampleRate, kOutputChannels, device_id)) {
            log_line("output monitor: failed to open waveOut device");
            output_monitor_enabled_ = false;
            return;
        }

        output_interleaved_.assign(static_cast<size_t>(buffer_size_ * kOutputChannels), 0.0f);
        output_monitor_enabled_ = true;
        log_line("output monitor: opened");
    }

    void write_output_monitor(ASIOLong index)
    {
        if (!output_monitor_enabled_ || output_buffers_.size() < kOutputChannels) {
            return;
        }

        output_interleaved_.resize(static_cast<size_t>(buffer_size_ * kOutputChannels));
        for (ASIOLong frame = 0; frame < buffer_size_; ++frame) {
            for (ASIOLong channel = 0; channel < kOutputChannels; ++channel) {
                output_interleaved_[static_cast<size_t>(frame * kOutputChannels + channel)] =
                    output_buffers_[channel][index][frame];
            }
        }

        if (!output_player_.write(output_interleaved_.data(), buffer_size_)) {
            log_line("output monitor: waveOut write failed");
            output_monitor_enabled_ = false;
        }
    }

    std::atomic<ULONG> ref_count_{1};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> sample_position_{0};
    std::string last_error_;
    ASIOCallbacks* callbacks_ = nullptr;
    ASIOLong buffer_size_ = kPreferredBufferSize;
    std::vector<ChannelDoubleBuffer> input_buffers_;
    std::vector<ChannelDoubleBuffer> output_buffers_;
    std::vector<float*> buffer_ptrs_;
    std::vector<float> output_interleaved_;
    ReacRingBuffer ring_;
    PcapCapture capture_;
    WaveOutPlayer output_player_;
    bool output_monitor_enabled_ = false;
    std::thread capture_thread_;
    std::thread callback_thread_;
};

class ReacClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (!out) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *out = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --ref_count_;
        if (refs == 0) {
            delete this;
        }
        return refs;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        ReacAsioDriver* driver = new ReacAsioDriver();
        const HRESULT hr = driver->QueryInterface(riid, out);
        driver->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        if (lock) {
            ++g_lock_count;
        } else {
            --g_lock_count;
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
};

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow()
{
    return g_object_count == 0 && g_lock_count == 0 ? S_OK : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** out)
{
    if (clsid != kDriverClsid) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    ReacClassFactory* factory = new ReacClassFactory();
    const HRESULT hr = factory->QueryInterface(riid, out);
    factory->Release();
    return hr;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer()
{
    const std::string path = module_path();
    const std::string clsid_key = std::string("Software\\Classes\\CLSID\\") + kClsidString;
    const std::string inproc_key = clsid_key + "\\InprocServer32";
    const std::string asio_key = std::string("Software\\ASIO\\") + kAsioRegistryName;

    bool ok = true;
    ok &= write_string_value(HKEY_CURRENT_USER, clsid_key.c_str(), nullptr, kDriverName);
    ok &= write_string_value(HKEY_CURRENT_USER, inproc_key.c_str(), nullptr, path.c_str());
    ok &= write_string_value(HKEY_CURRENT_USER, inproc_key.c_str(), "ThreadingModel", "Both");
    ok &= write_string_value(HKEY_CURRENT_USER, asio_key.c_str(), "CLSID", kClsidString);
    ok &= write_string_value(HKEY_CURRENT_USER, asio_key.c_str(), "Description", kDriverName);

    write_string_value(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\ASIO\\") + kAsioRegistryName).c_str(), "CLSID", kClsidString);
    write_string_value(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\ASIO\\") + kAsioRegistryName).c_str(), "Description", kDriverName);

    return ok ? S_OK : kSelfRegClassError;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer()
{
    delete_tree_if_present(HKEY_CURRENT_USER, (std::string("Software\\Classes\\CLSID\\") + kClsidString).c_str());
    delete_tree_if_present(HKEY_CURRENT_USER, (std::string("Software\\ASIO\\") + kAsioRegistryName).c_str());
    delete_tree_if_present(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\ASIO\\") + kAsioRegistryName).c_str());
    return S_OK;
}
