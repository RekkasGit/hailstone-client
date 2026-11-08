#include "mock_server/soe_endpoint.h"

#include <algorithm>
#include <stdexcept>

#include "core/byte_io.h"

namespace hailstone::test {

using soe::Bytes;
using soe::ByteSpan;

namespace {

std::uint8_t u8(std::byte b) { return std::to_integer<std::uint8_t>(b); }

std::uint16_t sequence_of(ByteSpan p) { return static_cast<std::uint16_t>(u8(p[2]) << 8 | u8(p[3])); }

} // namespace

soe::SessionParams SoeEndpoint::default_params() {
    soe::SessionParams params;
    params.encode_key = 0x12345678u;
    params.crc_bytes = 2;
    params.passes = {soe::EncodePass::None, soe::EncodePass::None};
    params.max_packet_size = 512;
    return params;
}

SoeEndpoint::SoeEndpoint(soe::SessionParams params, int opcode_bytes) : params_(params), opcode_bytes_(opcode_bytes) {
    std::string error;
    auto local = platform::Endpoint::v4(127, 0, 0, 1, 0);
    if (!socket_.open(platform::Endpoint::Family::V4, error, &local)) throw std::runtime_error("mock server: " + error);
}

std::optional<net::AppPacket> SoeEndpoint::take() {
    if (inbox.empty()) return std::nullopt;
    net::AppPacket p = std::move(inbox.front());
    inbox.pop_front();
    return p;
}

void SoeEndpoint::poll() {
    std::array<std::byte, 65536> buffer;
    platform::Endpoint from;
    while (auto size = socket_.recv_from(buffer, from)) on_datagram(ByteSpan(buffer.data(), *size), from);
}

// ---------------------------------------------------------------- session

void SoeEndpoint::start_session(ByteSpan request, const platform::Endpoint& from) {
    // 00 01, protocol version u32, connect code u32, max packet size u32 (all big-endian).
    if (request.size() < 14) return;
    ByteReader r(request);
    r.skip(2);
    std::uint32_t version = r.be<std::uint32_t>();
    std::uint32_t code = r.be<std::uint32_t>();
    std::uint32_t max_packet = r.be<std::uint32_t>();
    if (version != soe::kProtocolVersion) return;
    bool repeat = connected_ && from == client_ && code == connect_code_;
    if (!repeat) {
        client_ = from;
        connect_code_ = code;
        requested_max_packet_ = max_packet;
        connected_ = true;
        client_disconnected_ = false;
        ++sessions_;
        inbound_ = soe::InboundStream();
        next_out_ = 0;
        sent_.clear();
        last_ack_.reset();
        ack_log.clear();
        reliable_arrivals.clear();
        held_.clear();
    }
    // 00 02, connect code, encode key, CRC bytes, pass 1, pass 2, max packet size.
    ByteWriter w;
    w.le<std::uint8_t>(0);
    w.le<std::uint8_t>(soe::op::SessionResponse);
    w.be<std::uint32_t>(code);
    w.be<std::uint32_t>(params_.encode_key);
    w.le<std::uint8_t>(params_.crc_bytes);
    w.le<std::uint8_t>(static_cast<std::uint8_t>(params_.passes[0]));
    w.le<std::uint8_t>(static_cast<std::uint8_t>(params_.passes[1]));
    w.be<std::uint32_t>(params_.max_packet_size);
    send_raw(w.take());
}

void SoeEndpoint::on_datagram(ByteSpan d, const platform::Endpoint& from) {
    if (d.size() < 2) return;
    bool protocol = u8(d[0]) == 0;
    std::uint8_t opcode = u8(d[1]);
    if (protocol && opcode == soe::op::SessionRequest) return start_session(d, from);
    if (!connected_ || !(from == client_)) return;
    ++stats.datagrams_in;
    if (protocol && (opcode == soe::op::KeepAlive || opcode == soe::op::OutboundPing)) {
        if (opcode == soe::op::KeepAlive) ++stats.keepalives_in;
        return;
    }
    if (protocol && opcode == soe::op::SessionDisconnect) {
        client_disconnected_ = true;
        return;
    }
    auto packet = soe::decode_datagram(params_, d);
    if (!packet) {
        ++stats.bad_datagrams;
        return;
    }
    if (carries_reliable(*packet)) {
        // Arrivals are counted whatever the network then does to the datagram.
        std::vector<ByteSpan> parts;
        if (u8((*packet)[0]) == 0 && (*packet).size() >= 2 && u8((*packet)[1]) == soe::op::Combined) {
            if (auto split = soe::split_combined(*packet)) parts = *split;
        } else {
            parts.push_back(*packet);
        }
        for (auto part : parts)
            if (part.size() >= 4 && u8(part[0]) == 0 &&
                (u8(part[1]) == soe::op::Packet || u8(part[1]) == soe::op::Fragment))
                ++reliable_arrivals[sequence_of(part)];
        Fate fate = Fate::Deliver;
        if (!incoming_script_.empty()) {
            fate = incoming_script_.front();
            incoming_script_.pop_front();
        }
        if (fate == Fate::Drop) {
            ++stats.dropped_in;
            return;
        }
        process(*packet, 0);
        if (fate == Fate::Duplicate) process(*packet, 0);
        return;
    }
    process(*packet, 0);
}

bool SoeEndpoint::carries_reliable(ByteSpan p) const {
    if (p.size() < 2 || u8(p[0]) != 0) return false;
    if (u8(p[1]) == soe::op::Packet || u8(p[1]) == soe::op::Fragment) return true;
    if (u8(p[1]) == soe::op::Combined)
        if (auto parts = soe::split_combined(p))
            return std::any_of(parts->begin(), parts->end(), [&](ByteSpan part) { return carries_reliable(part); });
    return false;
}

void SoeEndpoint::process(ByteSpan p, int depth) {
    if (p.empty() || depth > 4) return;
    if (u8(p[0]) != 0 || (p.size() >= 2 && u8(p[1]) == soe::op::Padding)) {
        if (auto app = soe::decode_app(p, opcode_bytes_)) inbox.push_back(std::move(*app));
        return;
    }
    if (p.size() < 2) return;
    switch (u8(p[1])) {
    case soe::op::Combined:
    case soe::op::AppCombined: {
        auto parts = u8(p[1]) == soe::op::Combined ? soe::split_combined(p) : soe::split_app_combined(p);
        if (!parts) return;
        for (auto part : *parts) process(part, depth + 1);
        return;
    }
    case soe::op::Packet:
    case soe::op::Fragment: {
        if (p.size() < 4) return;
        std::vector<Bytes> ready;
        switch (inbound_.push(p, ready)) {
        case soe::SeqOrder::Future:
            ++stats.early_in;
            send_ack(soe::op::OutOfOrderAck, sequence_of(p));
            break;
        case soe::SeqOrder::Past:
            ++stats.duplicates_in;
            send_ack(soe::op::Ack, static_cast<std::uint16_t>(inbound_.next() - 1));
            break;
        case soe::SeqOrder::Current:
            send_ack(soe::op::Ack, static_cast<std::uint16_t>(inbound_.next() - 1));
            break;
        }
        for (auto& payload : ready) process(payload, depth + 1);
        return;
    }
    case soe::op::Ack: {
        if (p.size() < 4) return;
        std::uint16_t seq = sequence_of(p);
        ack_log.push_back({AckEvent::Kind::Ack, seq});
        if (!last_ack_ || soe::sequence_at_or_before(*last_ack_, seq)) last_ack_ = seq;
        for (auto& [s, sent] : sent_)
            if (soe::sequence_at_or_before(s, seq)) sent.acked = true;
        return;
    }
    case soe::op::OutOfOrderAck: {
        if (p.size() < 4) return;
        std::uint16_t seq = sequence_of(p);
        ack_log.push_back({AckEvent::Kind::OutOfOrder, seq});
        if (auto it = sent_.find(seq); it != sent_.end()) it->second.acked = true;
        return;
    }
    case soe::op::SessionStatRequest: {
        ++stats.stat_requests;
        // 40 bytes: the request's timestamp, our time, the client's counters echoed, ours.
        ByteWriter w;
        w.le<std::uint8_t>(0);
        w.le<std::uint8_t>(soe::op::SessionStatResponse);
        w.bytes(p.size() >= 4 ? p.subspan(2, 2) : ByteSpan{});
        if (p.size() < 4) w.zeros(2);
        w.be<std::uint32_t>(static_cast<std::uint32_t>(stats.datagrams_out));
        if (p.size() >= 40) w.bytes(p.subspan(24, 16));
        else w.zeros(16);
        w.be<std::uint64_t>(static_cast<std::uint64_t>(stats.datagrams_out));
        w.be<std::uint64_t>(static_cast<std::uint64_t>(stats.datagrams_in));
        transmit(w.take(), Fate::Deliver);
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------- sending

void SoeEndpoint::send_raw(const Bytes& datagram) {
    socket_.send_to(client_, datagram);
    ++stats.datagrams_out;
}

void SoeEndpoint::transmit(const Bytes& packet, Fate fate) {
    Bytes datagram = soe::encode_datagram(params_, packet);
    switch (fate) {
    case Fate::Deliver: send_raw(datagram); break;
    case Fate::Drop: break;
    case Fate::Duplicate:
        send_raw(datagram);
        send_raw(datagram);
        break;
    case Fate::Hold: held_.push_back(std::move(datagram)); break;
    }
}

Fate SoeEndpoint::next_outgoing_fate() {
    if (outgoing_script_.empty()) return Fate::Deliver;
    Fate f = outgoing_script_.front();
    outgoing_script_.pop_front();
    return f;
}

void SoeEndpoint::send_ack(std::uint8_t opcode, std::uint16_t sequence) {
    if (acks_to_drop_ > 0) {
        --acks_to_drop_;
        return;
    }
    transmit(soe::make_ack(opcode, sequence), Fate::Deliver);
}

void SoeEndpoint::release_held(bool reverse) {
    std::vector<Bytes> held = std::move(held_);
    held_.clear();
    if (reverse) std::reverse(held.begin(), held.end());
    for (auto& d : held) send_raw(d);
}

std::uint16_t SoeEndpoint::send_reliable(const net::AppPacket& packet) {
    Bytes wire = soe::encode_app(packet, opcode_bytes_);
    std::uint16_t first = next_out_;
    for (auto& piece : soe::make_reliable(wire, next_out_, params_)) {
        sent_[sequence_of(piece)] = {piece, false};
        transmit(piece, next_outgoing_fate());
    }
    return first;
}

void SoeEndpoint::send_reliable_combined(const std::vector<net::AppPacket>& packets) {
    std::vector<Bytes> parts;
    for (const auto& packet : packets) {
        auto pieces = soe::make_reliable(soe::encode_app(packet, opcode_bytes_), next_out_, params_);
        if (pieces.size() != 1 || pieces[0].size() > 255) throw std::logic_error("packet too big to combine");
        sent_[sequence_of(pieces[0])] = {pieces[0], false};
        parts.push_back(std::move(pieces[0]));
    }
    transmit(soe::make_combined(parts), next_outgoing_fate());
}

void SoeEndpoint::send_unreliable(const net::AppPacket& packet) {
    transmit(soe::encode_app(packet, opcode_bytes_), next_outgoing_fate());
}

std::size_t SoeEndpoint::resend_unacked() {
    std::size_t n = 0;
    for (auto& [seq, sent] : sent_) {
        if (sent.acked) continue;
        transmit(sent.packet, Fate::Deliver);
        ++stats.resent;
        ++n;
    }
    return n;
}

std::size_t SoeEndpoint::unacked() const {
    return static_cast<std::size_t>(
        std::count_if(sent_.begin(), sent_.end(), [](const auto& kv) { return !kv.second.acked; }));
}

bool SoeEndpoint::is_acked(std::uint16_t sequence) const {
    if (last_ack_ && soe::sequence_at_or_before(sequence, *last_ack_)) return true;
    auto it = sent_.find(sequence);
    return it != sent_.end() && it->second.acked;
}

void SoeEndpoint::send_keepalive() { transmit({std::byte{0}, std::byte{soe::op::KeepAlive}}, Fate::Deliver); }

// Encoded like any other packet (EQEmu's PacketCanBeEncoded exempts only the request, response and out-of-session).
void SoeEndpoint::send_disconnect() { transmit(soe::make_disconnect(connect_code_), Fate::Deliver); }

void SoeEndpoint::send_out_of_session() { send_raw({std::byte{0}, std::byte{soe::op::OutOfSession}}); }

} // namespace hailstone::test
