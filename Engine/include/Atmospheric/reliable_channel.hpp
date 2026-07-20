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
// All sequence/id comparisons use the wrap-safe (int16_t)(a - b) trick, so 16
// bits suffice as long as fewer than 32768 are in flight.
//
// Dependency-free (no engine/third-party headers) so it composes with
// DatagramSocket + NetConditioner and runs in the headless tests.
class ReliableChannel {
public:
    // Queue one application message.
    //   Reliable   — delivered exactly once, in send order.
    //   Unreliable — sent in the next packet, never resent, not ordered.
    void SendReliable(const uint8_t* data, int len);
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
    // order, then any unreliable ones — or 0 if none is ready. When non-null,
    // *outReliable reports which kind was returned.
    int Receive(uint8_t* buf, int maxLen, bool* outReliable = nullptr);

    // Reliable messages still awaiting acknowledgement (resent each packet).
    int UnackedCount() const {
        return static_cast<int>(_unacked.size());
    }

private:
    struct OutMsg {
        uint16_t id;
        std::vector<uint8_t> data;
    };
    struct SentPacket {
        uint16_t seq = 0;
        bool used = false;
        std::vector<uint16_t> msgIds;// reliable ids this packet carried
    };

    static constexpr int kSentHistory = 256;

    // Outbound.
    uint16_t _nextSeq = 0;
    uint16_t _nextMsgId = 0;
    std::deque<OutMsg> _unacked;        // reliable, resent until acked
    std::deque<std::vector<uint8_t>> _unrelOut;// unreliable, one send each
    SentPacket _sent[kSentHistory];     // seq % N -> reliable ids carried

    // Which of the PEER's packets we have received (to build our own acks).
    uint16_t _recvLatest = 0;
    uint32_t _recvBits = 0;
    bool _haveRecv = false;
    bool _ackOwed = false;

    // Inbound reliable reassembly (ordered) + unreliable passthrough.
    uint16_t _nextDeliverId = 0;
    std::map<uint16_t, std::vector<uint8_t>> _recvReliable;
    std::deque<std::vector<uint8_t>> _recvUnrel;

    void MarkAcked(uint16_t ackSeq);
};
