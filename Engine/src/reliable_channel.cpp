#include "reliable_channel.hpp"

#include <cstring>// memcpy

namespace {
    // Wrap-safe serial-number comparison (RFC 1982): a < b when the signed
    // 16-bit difference is negative. Correct across the uint16 wrap so long as
    // the two values are within 32768 of each other.
    inline bool SeqLess(uint16_t a, uint16_t b) {
        return static_cast<int16_t>(a - b) < 0;
    }
    inline bool SeqGreater(uint16_t a, uint16_t b) {
        return static_cast<int16_t>(a - b) > 0;
    }

    inline void PutU16(uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    inline void PutU32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = static_cast<uint8_t>(v >> 24);
    }
    inline uint16_t GetU16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }
    inline uint32_t GetU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
               | (static_cast<uint32_t>(p[3]) << 24);
    }

    // Wire layout:
    //   header (9 bytes): seq(2) ack(2) ackBits(4) msgCount(1)
    //   per message:      flags(1, bit0=reliable) channel(1) id(2) len(2) payload(len)
    constexpr int kHeaderLen = 9;
    constexpr int kMsgHeaderLen = 6;
}// namespace

bool ReliableChannel::SendReliable(const uint8_t* data, int len, uint8_t channel) {
    if (len < 0) return false;
    if (channel >= kNumChannels) return false;// unknown channel: reject, don't misroute
    if (static_cast<int>(_unacked.size()) >= kMaxInFlight) return false;// window full: backpressure
    OutMsg m;
    m.channel = channel;
    m.id = _nextMsgId[channel]++;// per-channel id space
    m.data.assign(data, data + len);
    _unacked.push_back(std::move(m));
    return true;
}

void ReliableChannel::SendUnreliable(const uint8_t* data, int len) {
    if (len < 0) return;
    _unrelOut.emplace_back(data, data + len);
}

int ReliableChannel::WritePacket(uint8_t* buf, int maxLen) {
    if (maxLen < kHeaderLen) return 0;

    const uint16_t seq = _nextSeq;
    int off = kHeaderLen;// leave room for the header, fill payload first
    uint8_t count = 0;
    SentPacket& sent = _sent[seq % kSentHistory];
    sent.seq = seq;
    sent.used = true;
    sent.msgRefs.clear();

    // Reliable first (oldest unacked), re-sent until acknowledged.
    for (const OutMsg& m : _unacked) {
        const int need = kMsgHeaderLen + static_cast<int>(m.data.size());
        if (off + need > maxLen || count == 255) break;
        buf[off] = 0x01;// reliable
        buf[off + 1] = m.channel;
        PutU16(buf + off + 2, m.id);
        PutU16(buf + off + 4, static_cast<uint16_t>(m.data.size()));
        std::memcpy(buf + off + kMsgHeaderLen, m.data.data(), m.data.size());
        off += need;
        sent.msgRefs.push_back({m.channel, m.id});
        count++;
    }
    // Then unreliable, consumed as they are sent.
    while (!_unrelOut.empty() && count < 255) {
        const std::vector<uint8_t>& m = _unrelOut.front();
        const int need = kMsgHeaderLen + static_cast<int>(m.size());
        if (off + need > maxLen) break;
        buf[off] = 0x00;// unreliable
        buf[off + 1] = 0;// channel unused for unreliable
        PutU16(buf + off + 2, 0);
        PutU16(buf + off + 4, static_cast<uint16_t>(m.size()));
        std::memcpy(buf + off + kMsgHeaderLen, m.data(), m.size());
        off += need;
        _unrelOut.pop_front();
        count++;
    }

    // Nothing to say and no ack owed: stay quiet.
    if (count == 0 && !_ackOwed) {
        sent.used = false;
        return 0;
    }

    const uint16_t ack = _recvLatest;
    const uint32_t ackBits = _haveRecv ? (_recvBits >> 1) : 0;// bit n => ack-1-n received
    PutU16(buf + 0, seq);
    PutU16(buf + 2, ack);
    PutU32(buf + 4, ackBits);
    buf[8] = count;

    _nextSeq++;
    _ackOwed = false;
    return off;
}

void ReliableChannel::MarkAcked(uint16_t ackSeq) {
    SentPacket& s = _sent[ackSeq % kSentHistory];
    if (!s.used || s.seq != ackSeq) return;
    for (const SentPacket::Ref& ref : s.msgRefs) {
        for (auto it = _unacked.begin(); it != _unacked.end(); ++it) {
            if (it->channel == ref.channel && it->id == ref.id) {
                _unacked.erase(it);
                break;
            }
        }
    }
    s.used = false;
}

void ReliableChannel::ReadPacket(const uint8_t* data, int len) {
    if (len < kHeaderLen) return;
    const uint16_t seq = GetU16(data + 0);
    const uint16_t ack = GetU16(data + 2);
    const uint32_t ackBits = GetU32(data + 4);
    const uint8_t count = data[8];

    // Record that we received `seq`, so future WritePacket acks reflect it.
    if (!_haveRecv) {
        _recvLatest = seq;
        _recvBits = 1;
        _haveRecv = true;
    } else if (SeqGreater(seq, _recvLatest)) {
        const uint16_t shift = static_cast<uint16_t>(seq - _recvLatest);
        _recvBits = (shift >= 32) ? 0u : (_recvBits << shift);
        _recvBits |= 1u;// bit0 = newest = this seq
        _recvLatest = seq;
    } else {
        const uint16_t back = static_cast<uint16_t>(_recvLatest - seq);
        if (back < 32) _recvBits |= (1u << back);// a gap being filled, or a dup
    }
    _ackOwed = true;

    // Retire our reliable messages the peer confirms (ack + its 32 predecessors).
    MarkAcked(ack);
    for (int n = 0; n < 32; n++)
        if (ackBits & (1u << n)) MarkAcked(static_cast<uint16_t>(ack - 1 - n));

    // Parse messages: buffer reliable ones for ordered release per channel,
    // dedupe by (channel, id).
    int off = kHeaderLen;
    for (uint8_t i = 0; i < count; i++) {
        if (off + kMsgHeaderLen > len) break;
        const uint8_t flags = data[off];
        const uint8_t channel = data[off + 1];
        const uint16_t id = GetU16(data + off + 2);
        const uint16_t mlen = GetU16(data + off + 4);
        off += kMsgHeaderLen;
        if (off + mlen > len) break;
        const uint8_t* payload = data + off;
        off += mlen;

        if (flags & 0x01) {
            if (channel >= kNumChannels) continue;             // unknown channel: drop
            if (SeqLess(id, _nextDeliverId[channel])) continue;// already delivered
            if (_recvReliable[channel].count(id)) continue;    // already buffered (dup)
            _recvReliable[channel][id].assign(payload, payload + mlen);
        } else {
            _recvUnrel.emplace_back(payload, payload + mlen);
        }
    }
}

int ReliableChannel::Receive(uint8_t* buf, int maxLen, bool* outReliable, uint8_t* outChannel) {
    // Reliable strictly in order, per channel: release _nextDeliverId[ch] for the
    // lowest channel that has it ready. Channels are independent, so a stall on
    // one never blocks another.
    for (int ch = 0; ch < kNumChannels; ch++) {
        auto it = _recvReliable[ch].find(_nextDeliverId[ch]);
        if (it != _recvReliable[ch].end()) {
            const std::vector<uint8_t>& m = it->second;
            int n = static_cast<int>(m.size());
            if (n > maxLen) n = maxLen;
            std::memcpy(buf, m.data(), static_cast<size_t>(n));
            _recvReliable[ch].erase(it);
            _nextDeliverId[ch]++;
            if (outReliable) *outReliable = true;
            if (outChannel) *outChannel = static_cast<uint8_t>(ch);
            return n;
        }
    }
    if (!_recvUnrel.empty()) {
        const std::vector<uint8_t>& m = _recvUnrel.front();
        int n = static_cast<int>(m.size());
        if (n > maxLen) n = maxLen;
        std::memcpy(buf, m.data(), static_cast<size_t>(n));
        _recvUnrel.pop_front();
        if (outReliable) *outReliable = false;
        if (outChannel) *outChannel = 0;
        return n;
    }
    return 0;
}
