#include "net/chat_server.h"

#include <mutex>

#include "core/byte_io.h"
#include "core/log.h"
#include "net/packet_io.h"
#include "net/session.h"
#include "platform/udp_socket.h"

namespace hailstone::net {

namespace {

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

AppPacket text_packet(std::uint16_t op, std::initializer_list<std::string_view> strings) {
    ByteWriter w;
    w.le<std::uint8_t>(0); // EQEmu's UCS skips the first byte of OP_MailLogin and OP_Mail
    for (auto s : strings) detail::write_cstring(w, s);
    return AppPacket(op, w.take());
}

} // namespace

AppPacket encode_mail_login(const ChatServerInfo& info) {
    std::string key = std::string(1, info.connection_type) + info.mail_key;
    return text_packet(ucs::kMailLogin, {"SOE.EQ." + info.server_short_name + "." + info.character, key});
}

AppPacket encode_mail_command(const std::string& text) { return text_packet(ucs::kMail, {text}); }

AppPacket encode_mail_bracket_command(const std::string& text) {
    ByteWriter w;
    std::string_view t = text;
    if (!t.empty() && t.front() == '[') {
        w.le<std::uint8_t>(';');
        t.remove_prefix(1);
    }
    detail::write_cstring(w, t);
    w.le<std::uint8_t>(0); // the empty argument string
    return AppPacket(ucs::kMail, w.take());
}

AppPacket encode_ucs_strings(std::uint16_t op, const std::vector<std::string>& strings) {
    ByteWriter w;
    for (const auto& s : strings) detail::write_cstring(w, s);
    return AppPacket(op, w.take());
}

std::optional<ChatServerEvent> decode_chat_server_packet(const AppPacket& p) {
    return detail::decode_with(p, [&](ByteReader& r) -> ChatServerEvent {
        ChatServerEvent e;
        switch (p.opcode) {
        case ucs::kChannelMessage: {
            e.kind = ChatServerEvent::Kind::Message;
            e.channel = r.cstring();
            e.sender = r.cstring();
            e.text = r.cstring();
            if (r.remaining()) e.fields.push_back(r.cstring()); // what follows ("SPAM:0:")
            return e;
        }
        case ucs::kMail:
            e.kind = ChatServerEvent::Kind::Channels;
            e.text = r.remaining() ? r.fixed_string(r.remaining()) : std::string();
            return e;
        case ucs::kBuddy:
            e.kind = ChatServerEvent::Kind::Buddy;
            e.added = r.le<std::uint8_t>() == 1;
            e.text = r.cstring();
            return e;
        case ucs::kIgnore:
            e.kind = ChatServerEvent::Kind::Ignore;
            e.added = r.le<std::uint8_t>() == 0;
            e.text = r.cstring();
            return e;
        case ucs::kMailLogin: {
            // Client::SendMailBoxes: u8 1, u32 count, u32 0, the comma list, a NUL.
            e.kind = ChatServerEvent::Kind::MailboxList;
            r.le<std::uint8_t>();
            r.le<std::uint32_t>();
            r.le<std::uint32_t>();
            std::string list = r.remaining() ? r.cstring() : std::string();
            for (auto& m : split(list, ',')) e.fields.push_back(trim(m));
            return e;
        }
        case ucs::kMailHeaderCount:
        case ucs::kMailHeader:
        case ucs::kMailSendBody:
        case ucs::kMailNew:
        case ucs::kMailDeliveryStatus:
        case ucs::kMailboxChange: {
            e.kind = p.opcode == ucs::kMailHeaderCount     ? ChatServerEvent::Kind::MailHeaderCount
                     : p.opcode == ucs::kMailHeader        ? ChatServerEvent::Kind::MailHeader
                     : p.opcode == ucs::kMailSendBody      ? ChatServerEvent::Kind::MailBody
                     : p.opcode == ucs::kMailNew           ? ChatServerEvent::Kind::MailNew
                     : p.opcode == ucs::kMailDeliveryStatus ? ChatServerEvent::Kind::MailDelivery
                                                           : ChatServerEvent::Kind::MailboxChange;
            // NUL-separated strings; the last one may lack its NUL (the body's "TO:" line).
            while (!r.empty()) {
                std::string f;
                while (!r.empty()) {
                    auto c = static_cast<char>(r.le<std::uint8_t>());
                    if (c == 0) break;
                    f.push_back(c);
                }
                e.fields.push_back(std::move(f));
            }
            return e;
        }
        case ucs::kAnnounceJoin:
        case ucs::kAnnounceLeave:
            e.kind = p.opcode == ucs::kAnnounceJoin ? ChatServerEvent::Kind::AnnounceJoin
                                                    : ChatServerEvent::Kind::AnnounceLeave;
            e.channel = r.cstring();
            e.sender = r.cstring();
            return e;
        default:
            throw std::out_of_range("unhandled chat server opcode");
        }
    });
}

struct ChatServer::Resolve {
    std::mutex mutex;
    bool done = false;
    std::vector<platform::Endpoint> endpoints;
    std::string error;
};

ChatServer::ChatServer() = default;
ChatServer::~ChatServer() { disconnect(); }

void ChatServer::connect(const ChatServerInfo& info) {
    disconnect();
    info_ = info;
    logged_in_ = false;
    // CEverQuest::ChatServerConnect runs while the player is in the zone: the name (from OP_SetChatServer2) is looked
    // up on a thread, and update() opens the session on its address. A lookup given up on (disconnect, another
    // connect) finishes on its own and is dropped; one still running at exit is left to the process's end
    // (resolve_async).
    auto r = std::make_shared<Resolve>();
    resolving_ = r;
    platform::resolve_async(info.host, info.port, [r](std::vector<platform::Endpoint> endpoints, std::string error) {
        std::lock_guard lock(r->mutex);
        r->endpoints = std::move(endpoints);
        r->error = std::move(error);
        r->done = true;
    });
    log::info("chat server: connecting to {}:{} as {}.{}", info.host, info.port, info.server_short_name, info.character);
}

void ChatServer::start_session(const std::string& address, std::uint16_t port) {
    SessionOptions opt;
    opt.opcode_bytes = 1;
    // UniversalChatProxy_Connect @0x7f0040 builds its own UdpManager Params: the keepalive delay (+0x38, the
    // UdpManager's 0xe) is 44 s, or 14.8 s with [Defaults] ChatKeepAlive set; the multi-buffer hold (+0x54) keeps
    // UdpLibrary's default, 50 ms. It sets no no-data timeout (0x12); the client counts 44 s of silence as a lost
    // link, which EQEmu's 9 s keepalive meets.
    opt.idle_timeout = std::chrono::milliseconds(44000);
    opt.keepalive_interval = std::chrono::milliseconds(44000);
    opt.hold_time = std::chrono::milliseconds(50);
    session_ = Session::create(opt);
    session_->connect(address, port); // a numeric address: no lookup on this thread
}

bool ChatServer::connected() const { return session_ && logged_in_ && session_->state() == SessionState::Connected; }

bool ChatServer::failed() const {
    if (resolving_) return false;
    return !session_ || session_->state() == SessionState::Disconnected;
}

void ChatServer::update() {
    if (resolving_) {
        std::vector<platform::Endpoint> endpoints;
        std::string error;
        {
            std::lock_guard lock(resolving_->mutex);
            if (!resolving_->done) return;
            endpoints = std::move(resolving_->endpoints);
            error = std::move(resolving_->error);
        }
        resolving_.reset();
        if (endpoints.empty()) {
            log::warn("chat server: {}", error);
            return; // failed(): no session
        }
        start_session(endpoints.front().address_string(), endpoints.front().port);
    }
    if (!session_) return;
    session_->update();
    if (!logged_in_ && session_->state() == SessionState::Connected) {
        session_->send(encode_mail_login(info_));
        logged_in_ = true;
        while (!pending_.empty()) {
            session_->send(pending_.front());
            pending_.pop_front();
        }
    }
    while (auto p = session_->receive()) {
        auto e = decode_chat_server_packet(*p);
        if (!e) {
            log::debug("chat server: opcode 0x{:02x} ({} bytes)", p->opcode, p->data.size());
            continue;
        }
        if (e->kind == ChatServerEvent::Kind::Channels) {
            channels_.clear();
            if (!e->text.empty())
                for (auto& c : split(e->text, ',')) channels_.push_back(trim(c));
        }
        events_.push_back(std::move(*e));
    }
}

void ChatServer::send_command(const std::string& text) {
    if (!session_ && !resolving_) return;
    if (!logged_in_) {
        pending_.push_back(encode_mail_command(text));
        return;
    }
    session_->send(encode_mail_command(text));
}

void ChatServer::send_mail_command(const std::string& text) {
    if (!session_ && !resolving_) return;
    if (!logged_in_) {
        pending_.push_back(encode_mail_bracket_command(text));
        return;
    }
    session_->send(encode_mail_bracket_command(text));
}

std::optional<ChatServerEvent> ChatServer::poll() {
    if (events_.empty()) return std::nullopt;
    auto e = std::move(events_.front());
    events_.pop_front();
    return e;
}

void ChatServer::disconnect() {
    resolving_.reset();
    if (!session_) return;
    session_->disconnect();
    for (int i = 0; i < 3; ++i) session_->update();
    session_.reset();
    logged_in_ = false;
}

} // namespace hailstone::net
