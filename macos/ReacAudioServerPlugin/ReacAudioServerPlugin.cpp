#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CoreFoundation.h>
#include <os/log.h>

#include "macos_pcap_capture.h"
#include "reac_decoder.h"
#include "reac_ring_buffer.h"

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr AudioObjectID kPluginObject = kAudioObjectPlugInObject;
constexpr AudioObjectID kDeviceObject = 2;
constexpr AudioObjectID kInputStreamObject = 3;
constexpr AudioObjectID kOutputStreamObject = 4;

constexpr UInt32 kInputChannelCount = 40;
constexpr UInt32 kOutputChannelCount = 2;
constexpr UInt32 kSampleRate = 48000;
constexpr UInt32 kBufferFrameSize = 512;
constexpr UInt32 kZeroTimestampPeriod = 24000;

constexpr const char* kBundleID = "com.reac.decoder.AudioServerPlugin";
constexpr const char* kDeviceUID = "com.reac.decoder.device";
constexpr const char* kModelUID = "com.reac.decoder.model";

AudioServerPlugInHostRef gHost = nullptr;
std::atomic<UInt32> gRefCount{1};
std::atomic<UInt32> gRunningClients{0};
std::atomic<UInt64> gZeroTimestampSeed{1};
std::atomic<Float64> gSampleTime{0.0};

extern AudioServerPlugInDriverInterface gDriverInterface;
extern AudioServerPlugInDriverInterface* gDriverInterfacePtr;

std::string cf_string_to_std(CFStringRef value)
{
    if (value == nullptr) {
        return {};
    }

    char buffer[256]{};
    if (CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return buffer;
    }
    return {};
}

std::string configured_capture_interface()
{
    CFPropertyListRef value = CFPreferencesCopyValue(CFSTR("captureInterface"),
                                                     CFSTR("com.reac.decoder"),
                                                     kCFPreferencesAnyUser,
                                                     kCFPreferencesCurrentHost);
    if (value == nullptr) {
        value = CFPreferencesCopyAppValue(CFSTR("captureInterface"), CFSTR("com.reac.decoder"));
    }
    if (value != nullptr) {
        std::string result;
        if (CFGetTypeID(value) == CFStringGetTypeID()) {
            result = cf_string_to_std(static_cast<CFStringRef>(value));
        }
        CFRelease(value);
        if (!result.empty()) {
            return result;
        }
    }

    const char* env_value = std::getenv("REAC_CAPTURE_INTERFACE");
    if (env_value != nullptr && env_value[0] != '\0') {
        return env_value;
    }

    return "en7";
}

class ReacCaptureEngine {
public:
    void start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }

        ring_.clear();
        decoded_packets_ = 0;
        skipped_packets_ = 0;
        running_ = true;
        capture_thread_ = std::thread([this] { capture_main(); });
    }

    void stop()
    {
        std::thread thread;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && !capture_thread_.joinable()) {
                return;
            }
            running_ = false;
            thread = std::move(capture_thread_);
        }

        if (thread.joinable()) {
            thread.join();
        }
    }

    void render_input(float* output, UInt32 frame_count)
    {
        ring_.read_interleaved(output, kInputChannelCount, frame_count);
    }

private:
    void capture_main()
    {
        MacPcapCapture capture;
        const std::string selector = configured_capture_interface();
        const std::vector<MacPcapDevice> devices = capture.list_devices();
        const std::string device_name = capture.resolve_device_name(selector, devices);
        if (device_name.empty()) {
            os_log_error(OS_LOG_DEFAULT,
                         "REAC capture could not resolve interface %{public}s",
                         selector.c_str());
            running_ = false;
            return;
        }

        if (!capture.open(device_name)) {
            os_log_error(OS_LOG_DEFAULT,
                         "REAC capture failed to open %{public}s: %{public}s",
                         device_name.c_str(),
                         capture.last_error().c_str());
            running_ = false;
            return;
        }

        os_log(OS_LOG_DEFAULT, "REAC capture listening on %{public}s", device_name.c_str());

        reac::Decoder decoder;
        reac::DecodedMultichannelPacket packet;
        capture.capture_loop([this, &decoder, &packet](const uint8_t* data, uint32_t size) {
            const reac::DecodeStatus status = decoder.decode_all(data, size, packet);
            if (status == reac::DecodeStatus::ok) {
                ring_.push(packet);
                const uint64_t decoded = ++decoded_packets_;
                if (decoded % 4000 == 0) {
                    os_log(OS_LOG_DEFAULT, "REAC capture decoded %{public}llu packets", decoded);
                }
            } else {
                const uint64_t skipped = ++skipped_packets_;
                if (skipped <= 10 || skipped % 1000 == 0) {
                    os_log_error(OS_LOG_DEFAULT,
                                 "REAC capture skipped packet: %{public}s",
                                 reac::to_string(status));
                }
            }
            return running_.load();
        });

        os_log(OS_LOG_DEFAULT,
               "REAC capture stopped decoded=%{public}llu skipped=%{public}llu",
               decoded_packets_.load(),
               skipped_packets_.load());
    }

    std::mutex mutex_;
    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> decoded_packets_{0};
    std::atomic<uint64_t> skipped_packets_{0};
    ReacRingBuffer ring_;
};

ReacCaptureEngine gCaptureEngine;

void selector_string(AudioObjectPropertySelector selector, char out[5])
{
    out[0] = static_cast<char>((selector >> 24) & 0xff);
    out[1] = static_cast<char>((selector >> 16) & 0xff);
    out[2] = static_cast<char>((selector >> 8) & 0xff);
    out[3] = static_cast<char>(selector & 0xff);
    out[4] = '\0';
    for (int index = 0; index < 4; ++index) {
        if (out[index] < 32 || out[index] > 126) {
            out[index] = '?';
        }
    }
}

void log_unknown_property(const char* method, AudioObjectID object_id, const AudioObjectPropertyAddress* address)
{
    if (address == nullptr) {
        os_log_error(OS_LOG_DEFAULT, "REAC %{public}s unknown null property address", method);
        return;
    }

    char selector[5];
    char scope[5];
    selector_string(address->mSelector, selector);
    selector_string(address->mScope, scope);
    os_log_error(OS_LOG_DEFAULT,
                 "REAC %{public}s unknown property selector=%{public}s object=%u scope=%{public}s element=%u",
                 method,
                 selector,
                 object_id,
                 scope,
                 address->mElement);
}

bool equal_uuid(REFIID lhs, CFUUIDRef rhs)
{
    const CFUUIDBytes rhs_bytes = CFUUIDGetUUIDBytes(rhs);
    return std::memcmp(&lhs, &rhs_bytes, sizeof(CFUUIDBytes)) == 0;
}

CFStringRef make_string(const char* value)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
}

AudioStreamBasicDescription stream_format(UInt32 channels)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = kSampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = sizeof(Float32) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(Float32) * channels;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 8 * sizeof(Float32);
    return format;
}

AudioStreamRangedDescription ranged_stream_format(UInt32 channels)
{
    AudioStreamRangedDescription description{};
    description.mFormat = stream_format(channels);
    description.mSampleRateRange.mMinimum = kSampleRate;
    description.mSampleRateRange.mMaximum = kSampleRate;
    return description;
}

template <typename T>
OSStatus write_data(UInt32 inDataSize, UInt32* outDataSize, void* outData, const T& value)
{
    if (inDataSize < sizeof(T)) {
        return kAudioHardwareBadPropertySizeError;
    }
    *static_cast<T*>(outData) = value;
    *outDataSize = sizeof(T);
    return noErr;
}

OSStatus write_cf_string(UInt32 inDataSize, UInt32* outDataSize, void* outData, const char* value)
{
    if (inDataSize < sizeof(CFStringRef)) {
        return kAudioHardwareBadPropertySizeError;
    }
    *static_cast<CFStringRef*>(outData) = make_string(value);
    *outDataSize = sizeof(CFStringRef);
    return noErr;
}

OSStatus write_ids(UInt32 inDataSize, UInt32* outDataSize, void* outData, const AudioObjectID* ids, UInt32 count)
{
    const UInt32 size = count * sizeof(AudioObjectID);
    if (inDataSize < size) {
        return kAudioHardwareBadPropertySizeError;
    }
    std::memcpy(outData, ids, size);
    *outDataSize = size;
    return noErr;
}

bool is_plugin(AudioObjectID object_id)
{
    return object_id == kPluginObject;
}

bool is_device(AudioObjectID object_id)
{
    return object_id == kDeviceObject;
}

bool is_stream(AudioObjectID object_id)
{
    return object_id == kInputStreamObject || object_id == kOutputStreamObject;
}

UInt32 stream_channels(AudioObjectID stream_id)
{
    return stream_id == kInputStreamObject ? kInputChannelCount : kOutputChannelCount;
}

UInt32 stream_direction(AudioObjectID stream_id)
{
    return stream_id == kInputStreamObject ? 1 : 0;
}

AudioClassID object_class(AudioObjectID object_id)
{
    if (is_plugin(object_id)) {
        return kAudioPlugInClassID;
    }
    if (is_device(object_id)) {
        return kAudioDeviceClassID;
    }
    if (is_stream(object_id)) {
        return kAudioStreamClassID;
    }
    return 0;
}

AudioObjectID object_owner(AudioObjectID object_id)
{
    if (is_plugin(object_id)) {
        return kAudioObjectUnknown;
    }
    if (is_device(object_id)) {
        return kPluginObject;
    }
    if (is_stream(object_id)) {
        return kDeviceObject;
    }
    return kAudioObjectUnknown;
}

const char* object_name(AudioObjectID object_id)
{
    switch (object_id) {
    case kPluginObject:
        return "REAC Audio Plugin";
    case kDeviceObject:
        return "REAC 40ch";
    case kInputStreamObject:
        return "REAC Inputs";
    case kOutputStreamObject:
        return "REAC Monitor Outputs";
    default:
        return "";
    }
}

bool has_base_property(AudioObjectID object_id, AudioObjectPropertySelector selector)
{
    if (!is_plugin(object_id) && !is_device(object_id) && !is_stream(object_id)) {
        return false;
    }

    switch (selector) {
    case kAudioObjectPropertyBaseClass:
    case kAudioObjectPropertyClass:
    case kAudioObjectPropertyOwner:
    case kAudioObjectPropertyName:
    case kAudioObjectPropertyModelName:
    case kAudioObjectPropertyManufacturer:
    case kAudioObjectPropertyOwnedObjects:
    case kAudioObjectPropertyCustomPropertyInfoList:
        return true;
    default:
        return false;
    }
}

UInt32 base_property_size(AudioObjectID object_id, AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioObjectPropertyBaseClass:
    case kAudioObjectPropertyClass:
        return sizeof(AudioClassID);
    case kAudioObjectPropertyOwner:
        return sizeof(AudioObjectID);
    case kAudioObjectPropertyName:
    case kAudioObjectPropertyModelName:
    case kAudioObjectPropertyManufacturer:
        return sizeof(CFStringRef);
    case kAudioObjectPropertyCustomPropertyInfoList:
        return 0;
    case kAudioObjectPropertyOwnedObjects:
        if (is_plugin(object_id)) {
            return sizeof(AudioObjectID);
        }
        if (is_device(object_id)) {
            return 2 * sizeof(AudioObjectID);
        }
        return 0;
    default:
        return 0;
    }
}

OSStatus get_base_property(AudioObjectID object_id,
                           AudioObjectPropertySelector selector,
                           UInt32 inDataSize,
                           UInt32* outDataSize,
                           void* outData)
{
    switch (selector) {
    case kAudioObjectPropertyBaseClass:
        return write_data(inDataSize, outDataSize, outData, object_class(object_id));
    case kAudioObjectPropertyClass:
        return write_data(inDataSize, outDataSize, outData, object_class(object_id));
    case kAudioObjectPropertyOwner:
        return write_data(inDataSize, outDataSize, outData, object_owner(object_id));
    case kAudioObjectPropertyName:
        return write_cf_string(inDataSize, outDataSize, outData, object_name(object_id));
    case kAudioObjectPropertyModelName:
        return write_cf_string(inDataSize, outDataSize, outData, "REAC Virtual Audio Device");
    case kAudioObjectPropertyManufacturer:
        return write_cf_string(inDataSize, outDataSize, outData, "REAC");
    case kAudioObjectPropertyCustomPropertyInfoList:
        *outDataSize = 0;
        return noErr;
    case kAudioObjectPropertyOwnedObjects:
        if (is_plugin(object_id)) {
            const AudioObjectID ids[] = { kDeviceObject };
            return write_ids(inDataSize, outDataSize, outData, ids, 1);
        }
        if (is_device(object_id)) {
            const AudioObjectID ids[] = { kInputStreamObject, kOutputStreamObject };
            return write_ids(inDataSize, outDataSize, outData, ids, 2);
        }
        *outDataSize = 0;
        return noErr;
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

bool has_plugin_property(AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioPlugInPropertyBundleID:
    case kAudioPlugInPropertyDeviceList:
    case kAudioPlugInPropertyTranslateUIDToDevice:
    case kAudioPlugInPropertyBoxList:
    case kAudioPlugInPropertyTranslateUIDToBox:
    case kAudioPlugInPropertyClockDeviceList:
    case kAudioPlugInPropertyTranslateUIDToClockDevice:
        return true;
    default:
        return false;
    }
}

UInt32 plugin_property_size(AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioPlugInPropertyBundleID:
        return sizeof(CFStringRef);
    case kAudioPlugInPropertyDeviceList:
    case kAudioPlugInPropertyTranslateUIDToDevice:
    case kAudioPlugInPropertyTranslateUIDToBox:
    case kAudioPlugInPropertyTranslateUIDToClockDevice:
        return sizeof(AudioObjectID);
    case kAudioPlugInPropertyBoxList:
    case kAudioPlugInPropertyClockDeviceList:
        return 0;
    default:
        return 0;
    }
}

OSStatus get_plugin_property(AudioObjectPropertySelector selector,
                             UInt32 inQualifierDataSize,
                             const void* inQualifierData,
                             UInt32 inDataSize,
                             UInt32* outDataSize,
                             void* outData)
{
    switch (selector) {
    case kAudioPlugInPropertyBundleID:
        return write_cf_string(inDataSize, outDataSize, outData, kBundleID);
    case kAudioPlugInPropertyDeviceList: {
        const AudioObjectID ids[] = { kDeviceObject };
        return write_ids(inDataSize, outDataSize, outData, ids, 1);
    }
    case kAudioPlugInPropertyTranslateUIDToDevice: {
        if (inQualifierDataSize != sizeof(CFStringRef) || inQualifierData == nullptr) {
            return kAudioHardwareBadPropertySizeError;
        }
        const CFStringRef uid = *static_cast<const CFStringRef*>(inQualifierData);
        const AudioObjectID device = CFStringCompare(uid, CFSTR("com.reac.decoder.device"), 0) == kCFCompareEqualTo
            ? kDeviceObject
            : kAudioObjectUnknown;
        return write_data(inDataSize, outDataSize, outData, device);
    }
    case kAudioPlugInPropertyBoxList:
    case kAudioPlugInPropertyClockDeviceList:
        *outDataSize = 0;
        return noErr;
    case kAudioPlugInPropertyTranslateUIDToBox:
    case kAudioPlugInPropertyTranslateUIDToClockDevice:
        return write_data(inDataSize, outDataSize, outData, static_cast<AudioObjectID>(kAudioObjectUnknown));
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

bool has_device_property(AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioDevicePropertyDeviceUID:
    case kAudioDevicePropertyModelUID:
    case kAudioDevicePropertyTransportType:
    case kAudioDevicePropertyClockDomain:
    case kAudioDevicePropertyClockAlgorithm:
    case kAudioDevicePropertyClockIsStable:
    case kAudioDevicePropertyZeroTimeStampPeriod:
    case kAudioDevicePropertyDeviceIsAlive:
    case kAudioDevicePropertyIsHidden:
    case kAudioDevicePropertyDeviceIsRunning:
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertyStreams:
    case kAudioObjectPropertyControlList:
    case kAudioDevicePropertySafetyOffset:
    case kAudioDevicePropertyNominalSampleRate:
    case kAudioDevicePropertyAvailableNominalSampleRates:
    case kAudioDevicePropertyBufferFrameSize:
    case kAudioDevicePropertyBufferFrameSizeRange:
    case kAudioDevicePropertyStreamConfiguration:
        return true;
    default:
        return false;
    }
}

UInt32 device_property_size(AudioObjectPropertySelector selector, AudioObjectPropertyScope scope)
{
    switch (selector) {
    case kAudioDevicePropertyDeviceUID:
    case kAudioDevicePropertyModelUID:
        return sizeof(CFStringRef);
    case kAudioDevicePropertyTransportType:
    case kAudioDevicePropertyClockDomain:
    case kAudioDevicePropertyClockAlgorithm:
    case kAudioDevicePropertyClockIsStable:
    case kAudioDevicePropertyZeroTimeStampPeriod:
    case kAudioDevicePropertyDeviceIsAlive:
    case kAudioDevicePropertyIsHidden:
    case kAudioDevicePropertyDeviceIsRunning:
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertySafetyOffset:
    case kAudioDevicePropertyBufferFrameSize:
        return sizeof(UInt32);
    case kAudioDevicePropertyStreams:
        if (scope == kAudioObjectPropertyScopeInput || scope == kAudioObjectPropertyScopeOutput) {
            return sizeof(AudioObjectID);
        }
        return 2 * sizeof(AudioObjectID);
    case kAudioObjectPropertyControlList:
        return 0;
    case kAudioDevicePropertyNominalSampleRate:
        return sizeof(Float64);
    case kAudioDevicePropertyAvailableNominalSampleRates:
    case kAudioDevicePropertyBufferFrameSizeRange:
        return sizeof(AudioValueRange);
    case kAudioDevicePropertyStreamConfiguration:
        return sizeof(AudioBufferList);
    default:
        return 0;
    }
}

OSStatus get_device_property(AudioObjectPropertySelector selector,
                             AudioObjectPropertyScope scope,
                             UInt32 inDataSize,
                             UInt32* outDataSize,
                             void* outData)
{
    switch (selector) {
    case kAudioDevicePropertyDeviceUID:
        return write_cf_string(inDataSize, outDataSize, outData, kDeviceUID);
    case kAudioDevicePropertyModelUID:
        return write_cf_string(inDataSize, outDataSize, outData, kModelUID);
    case kAudioDevicePropertyTransportType:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(kAudioDeviceTransportTypeVirtual));
    case kAudioDevicePropertyClockDomain:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(0));
    case kAudioDevicePropertyClockAlgorithm:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(kAudioDeviceClockAlgorithmRaw));
    case kAudioDevicePropertyClockIsStable:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(1));
    case kAudioDevicePropertyZeroTimeStampPeriod:
        return write_data(inDataSize, outDataSize, outData, kZeroTimestampPeriod);
    case kAudioDevicePropertyDeviceIsAlive:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(1));
    case kAudioDevicePropertyIsHidden:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(0));
    case kAudioDevicePropertyDeviceIsRunning:
        return write_data(inDataSize, outDataSize, outData, gRunningClients.load() > 0 ? static_cast<UInt32>(1) : static_cast<UInt32>(0));
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(1));
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        return write_data(inDataSize, outDataSize, outData, scope == kAudioObjectPropertyScopeOutput ? static_cast<UInt32>(1) : static_cast<UInt32>(0));
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertySafetyOffset:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(0));
    case kAudioDevicePropertyStreams:
        if (scope == kAudioObjectPropertyScopeInput) {
            const AudioObjectID ids[] = { kInputStreamObject };
            return write_ids(inDataSize, outDataSize, outData, ids, 1);
        }
        if (scope == kAudioObjectPropertyScopeOutput) {
            const AudioObjectID ids[] = { kOutputStreamObject };
            return write_ids(inDataSize, outDataSize, outData, ids, 1);
        }
        {
            const AudioObjectID ids[] = { kInputStreamObject, kOutputStreamObject };
            return write_ids(inDataSize, outDataSize, outData, ids, 2);
        }
    case kAudioObjectPropertyControlList:
        *outDataSize = 0;
        return noErr;
    case kAudioDevicePropertyNominalSampleRate:
        return write_data(inDataSize, outDataSize, outData, static_cast<Float64>(kSampleRate));
    case kAudioDevicePropertyAvailableNominalSampleRates: {
        AudioValueRange range{};
        range.mMinimum = kSampleRate;
        range.mMaximum = kSampleRate;
        return write_data(inDataSize, outDataSize, outData, range);
    }
    case kAudioDevicePropertyBufferFrameSize:
        return write_data(inDataSize, outDataSize, outData, kBufferFrameSize);
    case kAudioDevicePropertyBufferFrameSizeRange: {
        AudioValueRange range{};
        range.mMinimum = kBufferFrameSize;
        range.mMaximum = kBufferFrameSize;
        return write_data(inDataSize, outDataSize, outData, range);
    }
    case kAudioDevicePropertyStreamConfiguration: {
        if (inDataSize < sizeof(AudioBufferList)) {
            return kAudioHardwareBadPropertySizeError;
        }
        auto* list = static_cast<AudioBufferList*>(outData);
        list->mNumberBuffers = 1;
        list->mBuffers[0].mNumberChannels = scope == kAudioObjectPropertyScopeOutput ? kOutputChannelCount : kInputChannelCount;
        list->mBuffers[0].mDataByteSize = 0;
        list->mBuffers[0].mData = nullptr;
        *outDataSize = sizeof(AudioBufferList);
        return noErr;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

bool has_stream_property(AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioStreamPropertyIsActive:
    case kAudioStreamPropertyDirection:
    case kAudioStreamPropertyTerminalType:
    case kAudioStreamPropertyStartingChannel:
    case kAudioStreamPropertyLatency:
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyPhysicalFormat:
    case kAudioStreamPropertyAvailablePhysicalFormats:
        return true;
    default:
        return false;
    }
}

UInt32 stream_property_size(AudioObjectPropertySelector selector)
{
    switch (selector) {
    case kAudioStreamPropertyIsActive:
    case kAudioStreamPropertyDirection:
    case kAudioStreamPropertyTerminalType:
    case kAudioStreamPropertyStartingChannel:
    case kAudioStreamPropertyLatency:
        return sizeof(UInt32);
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
        return sizeof(AudioStreamBasicDescription);
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:
        return sizeof(AudioStreamRangedDescription);
    default:
        return 0;
    }
}

OSStatus get_stream_property(AudioObjectID object_id,
                             AudioObjectPropertySelector selector,
                             UInt32 inDataSize,
                             UInt32* outDataSize,
                             void* outData)
{
    const UInt32 channels = stream_channels(object_id);
    switch (selector) {
    case kAudioStreamPropertyIsActive:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(1));
    case kAudioStreamPropertyDirection:
        return write_data(inDataSize, outDataSize, outData, stream_direction(object_id));
    case kAudioStreamPropertyTerminalType:
        return write_data(inDataSize, outDataSize, outData, object_id == kInputStreamObject
            ? static_cast<UInt32>(kAudioStreamTerminalTypeMicrophone)
            : static_cast<UInt32>(kAudioStreamTerminalTypeSpeaker));
    case kAudioStreamPropertyStartingChannel:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(1));
    case kAudioStreamPropertyLatency:
        return write_data(inDataSize, outDataSize, outData, static_cast<UInt32>(0));
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat:
        return write_data(inDataSize, outDataSize, outData, stream_format(channels));
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats:
        return write_data(inDataSize, outDataSize, outData, ranged_stream_format(channels));
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

HRESULT STDMETHODCALLTYPE query_interface(void*, REFIID inUUID, LPVOID* outInterface)
{
    if (outInterface == nullptr) {
        return E_POINTER;
    }

    if (equal_uuid(inUUID, IUnknownUUID) || equal_uuid(inUUID, kAudioServerPlugInDriverInterfaceUUID)) {
        ++gRefCount;
        *outInterface = &gDriverInterfacePtr;
        return S_OK;
    }

    *outInterface = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE add_ref(void*)
{
    return ++gRefCount;
}

ULONG STDMETHODCALLTYPE release(void*)
{
    return --gRefCount;
}

OSStatus STDMETHODCALLTYPE initialize(AudioServerPlugInDriverRef, AudioServerPlugInHostRef inHost)
{
    gHost = inHost;
    return noErr;
}

OSStatus STDMETHODCALLTYPE create_device(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*)
{
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus STDMETHODCALLTYPE destroy_device(AudioServerPlugInDriverRef, AudioObjectID)
{
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus STDMETHODCALLTYPE add_device_client(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*)
{
    return noErr;
}

OSStatus STDMETHODCALLTYPE remove_device_client(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*)
{
    return noErr;
}

OSStatus STDMETHODCALLTYPE perform_config_change(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*)
{
    return noErr;
}

OSStatus STDMETHODCALLTYPE abort_config_change(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*)
{
    return noErr;
}

Boolean STDMETHODCALLTYPE has_property(AudioServerPlugInDriverRef,
                                       AudioObjectID inObjectID,
                                       pid_t,
                                       const AudioObjectPropertyAddress* inAddress)
{
    if (inAddress == nullptr) {
        return false;
    }
    if (has_base_property(inObjectID, inAddress->mSelector)) {
        return true;
    }
    if (is_plugin(inObjectID)) {
        return has_plugin_property(inAddress->mSelector);
    }
    if (is_device(inObjectID)) {
        return has_device_property(inAddress->mSelector);
    }
    if (is_stream(inObjectID)) {
        return has_stream_property(inAddress->mSelector);
    }
    log_unknown_property("HasProperty", inObjectID, inAddress);
    return false;
}

OSStatus STDMETHODCALLTYPE is_property_settable(AudioServerPlugInDriverRef,
                                                AudioObjectID,
                                                pid_t,
                                                const AudioObjectPropertyAddress*,
                                                Boolean* outIsSettable)
{
    if (outIsSettable == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    *outIsSettable = false;
    return noErr;
}

OSStatus STDMETHODCALLTYPE get_property_data_size(AudioServerPlugInDriverRef,
                                                  AudioObjectID inObjectID,
                                                  pid_t,
                                                  const AudioObjectPropertyAddress* inAddress,
                                                  UInt32,
                                                  const void*,
                                                  UInt32* outDataSize)
{
    if (inAddress == nullptr || outDataSize == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }

    if (has_base_property(inObjectID, inAddress->mSelector)) {
        *outDataSize = base_property_size(inObjectID, inAddress->mSelector);
        return noErr;
    }
    if (is_plugin(inObjectID) && has_plugin_property(inAddress->mSelector)) {
        *outDataSize = plugin_property_size(inAddress->mSelector);
        return noErr;
    }
    if (is_device(inObjectID) && has_device_property(inAddress->mSelector)) {
        *outDataSize = device_property_size(inAddress->mSelector, inAddress->mScope);
        return noErr;
    }
    if (is_stream(inObjectID) && has_stream_property(inAddress->mSelector)) {
        *outDataSize = stream_property_size(inAddress->mSelector);
        return noErr;
    }

    log_unknown_property("GetPropertyDataSize", inObjectID, inAddress);
    return kAudioHardwareUnknownPropertyError;
}

OSStatus STDMETHODCALLTYPE get_property_data(AudioServerPlugInDriverRef,
                                             AudioObjectID inObjectID,
                                             pid_t,
                                             const AudioObjectPropertyAddress* inAddress,
                                             UInt32 inQualifierDataSize,
                                             const void* inQualifierData,
                                             UInt32 inDataSize,
                                             UInt32* outDataSize,
                                             void* outData)
{
    if (inAddress == nullptr || outDataSize == nullptr || outData == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }

    if (has_base_property(inObjectID, inAddress->mSelector)) {
        return get_base_property(inObjectID, inAddress->mSelector, inDataSize, outDataSize, outData);
    }
    if (is_plugin(inObjectID) && has_plugin_property(inAddress->mSelector)) {
        return get_plugin_property(inAddress->mSelector, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
    }
    if (is_device(inObjectID) && has_device_property(inAddress->mSelector)) {
        return get_device_property(inAddress->mSelector, inAddress->mScope, inDataSize, outDataSize, outData);
    }
    if (is_stream(inObjectID) && has_stream_property(inAddress->mSelector)) {
        return get_stream_property(inObjectID, inAddress->mSelector, inDataSize, outDataSize, outData);
    }

    log_unknown_property("GetPropertyData", inObjectID, inAddress);
    return kAudioHardwareUnknownPropertyError;
}

OSStatus STDMETHODCALLTYPE set_property_data(AudioServerPlugInDriverRef,
                                             AudioObjectID,
                                             pid_t,
                                             const AudioObjectPropertyAddress*,
                                             UInt32,
                                             const void*,
                                             UInt32,
                                             const void*)
{
    return kAudioHardwareUnsupportedOperationError;
}

OSStatus STDMETHODCALLTYPE start_io(AudioServerPlugInDriverRef, AudioObjectID, UInt32)
{
    if (gRunningClients.fetch_add(1) == 0) {
        gSampleTime = 0.0;
        ++gZeroTimestampSeed;
        gCaptureEngine.start();
    }
    return noErr;
}

OSStatus STDMETHODCALLTYPE stop_io(AudioServerPlugInDriverRef, AudioObjectID, UInt32)
{
    UInt32 count = gRunningClients.load();
    while (count > 0 && !gRunningClients.compare_exchange_weak(count, count - 1)) {
    }
    if (count == 1) {
        gCaptureEngine.stop();
    }
    return noErr;
}

OSStatus STDMETHODCALLTYPE get_zero_timestamp(AudioServerPlugInDriverRef,
                                              AudioObjectID,
                                              UInt32,
                                              Float64* outSampleTime,
                                              UInt64* outHostTime,
                                              UInt64* outSeed)
{
    if (outSampleTime == nullptr || outHostTime == nullptr || outSeed == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    *outSampleTime = gSampleTime.load();
    *outHostTime = AudioGetCurrentHostTime();
    *outSeed = gZeroTimestampSeed.load();
    return noErr;
}

OSStatus STDMETHODCALLTYPE will_do_io_operation(AudioServerPlugInDriverRef,
                                                AudioObjectID,
                                                UInt32,
                                                UInt32 inOperationID,
                                                Boolean* outWillDo,
                                                Boolean* outWillDoInPlace)
{
    if (outWillDo == nullptr || outWillDoInPlace == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    *outWillDo = inOperationID == kAudioServerPlugInIOOperationReadInput
        || inOperationID == kAudioServerPlugInIOOperationWriteMix;
    *outWillDoInPlace = true;
    return noErr;
}

OSStatus STDMETHODCALLTYPE begin_io_operation(AudioServerPlugInDriverRef,
                                              AudioObjectID,
                                              UInt32,
                                              UInt32,
                                              UInt32,
                                              const AudioServerPlugInIOCycleInfo*)
{
    return noErr;
}

OSStatus STDMETHODCALLTYPE do_io_operation(AudioServerPlugInDriverRef,
                                           AudioObjectID,
                                           AudioObjectID inStreamObjectID,
                                           UInt32,
                                           UInt32 inOperationID,
                                           UInt32 inIOBufferFrameSize,
                                           const AudioServerPlugInIOCycleInfo*,
                                           void* ioMainBuffer,
                                           void*)
{
    if (ioMainBuffer == nullptr) {
        return noErr;
    }

    if (inOperationID == kAudioServerPlugInIOOperationReadInput && inStreamObjectID == kInputStreamObject) {
        gCaptureEngine.render_input(static_cast<float*>(ioMainBuffer), inIOBufferFrameSize);
        gSampleTime = gSampleTime.load() + inIOBufferFrameSize;
    }
    return noErr;
}

OSStatus STDMETHODCALLTYPE end_io_operation(AudioServerPlugInDriverRef,
                                            AudioObjectID,
                                            UInt32,
                                            UInt32,
                                            UInt32,
                                            const AudioServerPlugInIOCycleInfo*)
{
    return noErr;
}

} // namespace

extern "C" __attribute__((visibility("default"))) void* AudioServerPlugInFactory(CFAllocatorRef, CFUUIDRef inTypeUUID)
{
    if (CFEqual(inTypeUUID, kAudioServerPlugInTypeUUID)) {
        return &gDriverInterfacePtr;
    }
    return nullptr;
}

namespace {

AudioServerPlugInDriverInterface gDriverInterface = {
    nullptr,
    query_interface,
    add_ref,
    release,
    initialize,
    create_device,
    destroy_device,
    add_device_client,
    remove_device_client,
    perform_config_change,
    abort_config_change,
    has_property,
    is_property_settable,
    get_property_data_size,
    get_property_data,
    set_property_data,
    start_io,
    stop_io,
    get_zero_timestamp,
    will_do_io_operation,
    begin_io_operation,
    do_io_operation,
    end_io_operation,
};

AudioServerPlugInDriverInterface* gDriverInterfacePtr = &gDriverInterface;

} // namespace
