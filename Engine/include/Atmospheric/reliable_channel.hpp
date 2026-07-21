#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

// ReliableChannel — sequencing, acknowledgement, and reliable-ordered messages
// on top of an unreliable datagram transport (DatagramSocket). This is the
// "reliable UDP" layer a game protocol builds for the messages that MUST arrive
// exactly once and in order — join/leave, score, chat, match-end — as opposed
// to per-tick state, which is deliberately left unreliable (a lost snapshot is
// just superseded by the next one, so resending it would only add latency).
//
// Design (Glenn Fiedler / reliable.io lineage):
//   - Every outgoing packet carries a 16-bit sequence number, an ack (the
//     latest sequence seen from the peer), and a 32-bit ack_bits (the 32
//     sequences before that). One packet therefore acknowledges up to 33 of the
//     peer's packets, so acknowledgements themselves survive packet loss.
//   - Reliable messages get a 16-bit id and are re-included in every outgoing
//     packet until an ack proves the peer received a packet carrying them.
//     Redundancy, not a retransmit timer, is what covers loss.
//   - The receiver dedupes by message id and releases messages in id order,
//     buffering anything that arrives early. That is precisely what turns a
//     lossy, reordering, duplicating link into reliable-ordered delivery.
//
// Channels (kNumChannels independent reliable-ordered streams):
//   Ordering is guaranteed *within* a channel, not across channels. Each channel
//   has its own id counter, delivery cursor, and reorder buffer, so a message
//   stalled on one channel (waiting for a lost predecessor's resend) does NOT
//   hold back messages on another. This is the ENet-style escape from
//   cross-stream head-of-line blocking: put logically-independent traffic on
//   separate channels (e.g. chat on 1, scoreboard on 2) and a lost chat line
//   can't delay a score update. The seq/ack layer stays global — one packet
//   still acks the peer regardless of which channels its messages belonged to.
//   channel defaults to 0, so callers that don't care get a single stream.
//
// All sequence/id comparisons use the wrap-safe (int16_t)(a - b) trick, so 16
// bits suffice as long as fewer than 32768 are in flight (per channel).
//
// Dependency-free (no engine/third-party headers) so it composes with
// DatagramSocket + NetConditioner and runs in the headless tests.
class ReliableChannel {
public:
    static constexpr int kNumChannels = 4;// independent reliable-ordered streams

    // Queue one application message.
    //   Reliable   — delivered exactly once, in send order *within its channel*.
    //     Returns false when the in-flight window (kMaxInFlight) is full or the
    //     channel is out of range: the message was NOT queued, and the caller
    //     should retry later. This bounds memory and applies backpressure instead
    //     of letting the unacked queue grow without limit when the app produces
    //     faster than the link acknowledges.
    //   Unreliable — sent in the next packet, never resent, not ordered. Always
    //     accepted (it is drained the moment it is next written). Channel-agnostic.
    [[nodiscard]] bool SendReliable(const uint8_t* data, int len, uint8_t channel = 0);
    void SendUnreliable(const uint8_t* data, int len);

    // Assemble the next outbound packet (header + as many pending messages as
    // fit) into buf. Returns bytes written, or 0 when there is nothing to send
    // (no messages pending and no acknowledgement owed). Call once per tick;
    // hand the bytes to DatagramSocket::SendTo.
    int WritePacket(uint8_t* buf, int maxLen);

    // Ingest a received packet: process its acks, then dedupe and buffer its
    // messages. Feed it whatever DatagramSocket::RecvFrom produced.
    void ReadPacket(const uint8_t* data, int len);

    // Pop the next message ready for the application — reliable ones strictly in
    // order per channel (lower channels drained first), then any unreliable ones
    // — or 0 if none is ready. When non-null, *outReliable reports which kind was
    // returned and *outChannel the channel a reliable message came from (0 for
    // unreliable).
    int Receive(uint8_t* buf, int maxLen, bool* outReliable = nullptr, uint8_t* outChannel = nullptr);

    // Reliable messages still awaiting acknowledgement (across all channels,
    // resent each packet).
    int UnackedCount() const {
        return static_cast<int>(_unacked.size());
    }

private:
    struct OutMsg {
        uint8_t channel;
        uint16_t id;
        std::vector<uint8_t> data;
    };
    struct SentPacket {
        uint16_t seq = 0;
        bool used = false;
        struct Ref {
            uint8_t channel;
            uint16_t id;
        };
        std::vector<Ref> msgRefs;// reliable (channel,id) pairs this packet carried
    };

    static constexpr int kSentHistory = 256;  // packets remembered for ack -> message-id lookup
    static constexpr int kMaxInFlight = 256;   // cap on unacked reliable messages (bounded memory / backpressure)

    // Outbound.
    uint16_t _nextSeq = 0;
    uint16_t _nextMsgId[kNumChannels] = {};// per-channel id counter
    std::deque<OutMsg> _unacked;        // reliable (all channels), resent until acked
    std::deque<std::vector<uint8_t>> _unrelOut;// unreliable, one send each
    SentPacket _sent[kSentHistory];     // seq % N -> reliable (channel,id) carried

    // Which of the PEER's packets we have received (to build our own acks).
    uint16_t _recvLatest = 0;
    uint32_t _recvBits = 0;
    bool _haveRecv = false;
    bool _ackOwed = false;

    // Inbound reliable reassembly (ordered, per channel) + unreliable passthrough.
    uint16_t _nextDeliverId[kNumChannels] = {};
    std::map<uint16_t, std::vector<uint8_t>> _recvReliable[kNumChannels];
    std::deque<std::vector<uint8_t>> _recvUnrel;

    void MarkAcked(uint16_t ackSeq);
};
