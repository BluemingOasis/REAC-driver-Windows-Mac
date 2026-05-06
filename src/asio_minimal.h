#pragma once

#include <cstdint>

#include <unknwn.h>

using ASIOBool = long;
using ASIOError = long;
using ASIOFuture = long;
using ASIOLong = long;
using ASIOSampleRate = double;

constexpr ASIOBool ASIOFalse = 0;
constexpr ASIOBool ASIOTrue = 1;

constexpr ASIOError ASE_OK = 0;
constexpr ASIOError ASE_SUCCESS = 0x3f4847a0;
constexpr ASIOError ASE_NotPresent = -1000;
constexpr ASIOError ASE_HWMalfunction = ASE_NotPresent + 1;
constexpr ASIOError ASE_InvalidParameter = ASE_NotPresent + 2;
constexpr ASIOError ASE_InvalidMode = ASE_NotPresent + 3;
constexpr ASIOError ASE_SPNotAdvancing = ASE_NotPresent + 4;
constexpr ASIOError ASE_NoClock = ASE_NotPresent + 5;
constexpr ASIOError ASE_NoMemory = ASE_NotPresent + 6;

constexpr ASIOLong ASIOSTFloat32LSB = 19;

struct ASIOSamples {
    uint32_t hi;
    uint32_t lo;
};

struct ASIOTimeStamp {
    uint32_t hi;
    uint32_t lo;
};

struct ASIOChannelInfo {
    ASIOLong channel;
    ASIOBool isInput;
    ASIOBool isActive;
    ASIOLong channelGroup;
    ASIOLong type;
    char name[32];
};

struct ASIOBufferInfo {
    ASIOBool isInput;
    ASIOLong channelNum;
    void* buffers[2];
};

struct ASIOClockSource {
    ASIOLong index;
    ASIOLong associatedChannel;
    ASIOLong associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOTimeInfo {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samplePosition;
    ASIOSampleRate sampleRate;
    ASIOLong flags;
    char reserved[12];
};

struct ASIOTimeCode {
    double speed;
    ASIOSamples timeCodeSamples;
    unsigned long flags;
    char future[64];
};

struct ASIOTime {
    ASIOLong reserved[4];
    ASIOTimeInfo timeInfo;
    ASIOTimeCode timeCode;
};

struct ASIOCallbacks {
    void (*bufferSwitch)(ASIOLong doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    ASIOLong (*asioMessage)(ASIOLong selector, ASIOLong value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, ASIOLong doubleBufferIndex, ASIOBool directProcess);
};

struct IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual ASIOLong getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(ASIOLong* numInputChannels, ASIOLong* numOutputChannels) = 0;
    virtual ASIOError getLatencies(ASIOLong* inputLatency, ASIOLong* outputLatency) = 0;
    virtual ASIOError getBufferSize(ASIOLong* minSize, ASIOLong* maxSize, ASIOLong* preferredSize, ASIOLong* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, ASIOLong* numSources) = 0;
    virtual ASIOError setClockSource(ASIOLong reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, ASIOLong numChannels, ASIOLong bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(ASIOFuture selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};
