#include "reac_decoder.h"

namespace reac {

DecodeStatus Decoder::decode_stereo(const uint8_t* packet,
                                    size_t packet_size,
                                    int left_channel,
                                    int right_channel,
                                    DecodedPacket& out) const
{
    if (left_channel < 0 || left_channel >= kChannelCount ||
        right_channel < 0 || right_channel >= kChannelCount) {
        return DecodeStatus::invalid_channel;
    }

    const DecodeStatus validation = validate_packet(packet, packet_size);
    if (validation != DecodeStatus::ok) {
        return validation;
    }

    out.counter = static_cast<uint16_t>(packet[14] | (packet[15] << 8));

    const uint8_t* payload = packet + kPayloadOffset;
    for (int sample = 0; sample < kSamplesPerPacket; ++sample) {
        out.left[sample] = decode_sample(payload, left_channel, sample);
        out.right[sample] = decode_sample(payload, right_channel, sample);
    }

    return DecodeStatus::ok;
}

DecodeStatus Decoder::decode_all(const uint8_t* packet,
                                 size_t packet_size,
                                 DecodedMultichannelPacket& out) const
{
    const DecodeStatus validation = validate_packet(packet, packet_size);
    if (validation != DecodeStatus::ok) {
        return validation;
    }

    out.counter = static_cast<uint16_t>(packet[14] | (packet[15] << 8));

    const uint8_t* payload = packet + kPayloadOffset;
    for (int channel = 0; channel < kChannelCount; ++channel) {
        for (int sample = 0; sample < kSamplesPerPacket; ++sample) {
            out.channels[channel][sample] = decode_sample(payload, channel, sample);
        }
    }

    return DecodeStatus::ok;
}

DecodeStatus Decoder::validate_packet(const uint8_t* packet, size_t packet_size)
{
    if (packet_size < kMinimumPacketBytes) {
        return DecodeStatus::too_short;
    }

    if (packet[12] != 0x88 || packet[13] != 0x19) {
        return DecodeStatus::wrong_ethertype;
    }

    if (packet[packet_size - 2] != 0xC2 || packet[packet_size - 1] != 0xEA) {
        return DecodeStatus::bad_trailer;
    }

    return DecodeStatus::ok;
}

float Decoder::decode_sample(const uint8_t* payload, int channel, int sample_index)
{
    const uint8_t* base = payload + sample_index * kChannelCount * kBytesPerSample;
    const uint8_t* pair = base + (channel & ~1) * kBytesPerSample;

    uint32_t raw = 0;
    if ((channel & 1) == 0) {
        raw = static_cast<uint32_t>(pair[3]) |
              (static_cast<uint32_t>(pair[0]) << 8) |
              (static_cast<uint32_t>(pair[1]) << 16);
    } else {
        raw = static_cast<uint32_t>(pair[4]) |
              (static_cast<uint32_t>(pair[5]) << 8) |
              (static_cast<uint32_t>(pair[2]) << 16);
    }

    const int32_t signed24 = (raw & 0x800000) ? static_cast<int32_t>(raw) - 0x1000000
                                             : static_cast<int32_t>(raw);
    return static_cast<float>(signed24) / 8388608.0f;
}

const char* to_string(DecodeStatus status)
{
    switch (status) {
    case DecodeStatus::ok:
        return "ok";
    case DecodeStatus::too_short:
        return "packet too short";
    case DecodeStatus::wrong_ethertype:
        return "wrong EtherType";
    case DecodeStatus::bad_trailer:
        return "bad REAC trailer";
    case DecodeStatus::invalid_channel:
        return "invalid channel";
    }
    return "unknown";
}

} // namespace reac
