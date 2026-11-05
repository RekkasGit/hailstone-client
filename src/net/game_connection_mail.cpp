// GameConnection: in-game mail over the chat server (MailMgr, CMailWnd and
// CMailCompositionWnd in the original). Commands are MailMgr's text commands
// on the chat session (net/chat_server.h encode_mail_bracket_command); the
// answers arrive as chat server events. State and rules in game/mail.h.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

#include "core/log.h"
#include "net/chat_server.h"
#include "net/game_connection.h"

namespace nmsc::net {

namespace {

std::uint32_t to_u32(const std::string& s) { return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 10)); }

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

std::string GameConnection::mail_server() const {
    // __ServerName: the server's short name, as in the chat login's mailbox "SOE.EQ.<short>.<name>".
    return chat_server_info_ ? chat_server_info_->server_short_name : ticket_.server_name;
}

bool GameConnection::mail_send_command(const std::string& text) {
    // Every MailMgr command first checks the chat service (eqstr 4529 without it).
    if (!chat_server_ || !chat_server_->connected()) {
        system(4529);
        return false;
    }
    chat_server_->send_mail_command(text);
    return true;
}

void GameConnection::mail_set_forwarding(bool on) {
    // MailMgr_SetMailForwarding @0x469cc0: "[setmailforwarding 0|1" when there is a chat service, else nothing
    // (no 4529 line). EQEmu's UCS takes the command and does nothing with it.
    if (chat_server_ && chat_server_->connected()) chat_server_->send_mail_command(std::format("[setmailforwarding {}", on ? 1 : 0));
}

void GameConnection::mail_open() {
    // CMailWnd::AboutToShow: selects the current mailbox again, which makes the
    // server answer with OP_MailboxChange and the headers follow.
    mail_.window_open = true;
    mail_select_mailbox(mail_.current);
}

void GameConnection::mail_close() { mail_.window_open = false; }

void GameConnection::mail_select_mailbox(int index) {
    // MailMgr_SelectMailbox @0x46ab80: "[selectmailbox <n>".
    mail_send_command(std::format("[selectmailbox {}", index));
}

void GameConnection::mail_request_headers() {
    // MailMgr_RequestHeaders @0x46c010: the headers are dropped, "[getheaders".
    mail_.headers.clear();
    mail_.expected = -1;
    mail_.unread = false;
    mail_send_command("[getheaders");
}

bool GameConnection::mail_request_body(std::uint32_t id) {
    // MailMgr_RequestMessageBody @0x46ad00: "[getbody <id>" unless it came already.
    auto it = mail_.headers.find(id);
    if (it == mail_.headers.end()) return false;
    if (it->second.body) return true;
    return mail_send_command(std::format("[getbody {}", id));
}

void GameConnection::mail_set_status(const std::vector<std::uint32_t>& ids, game::MailStatus status) {
    // MailMgr_SetMessageStatus @0x46af20: "[setmessagestatus <WORD> id id ...";
    // the local headers take the status (deleted ones go).
    if (ids.empty()) return;
    std::string cmd = std::string("[setmessagestatus ") + game::mail_status_word(status);
    for (auto id : ids) cmd += std::format(" {}", id);
    if (!mail_send_command(cmd)) return;
    for (auto id : ids) {
        auto it = mail_.headers.find(id);
        if (it == mail_.headers.end()) continue;
        if (status == game::MailStatus::Deleted) mail_.headers.erase(it);
        else it->second.status = status;
    }
    mail_.unread = false;
    for (const auto& [id, h] : mail_.headers)
        if (h.status == game::MailStatus::New || h.status == game::MailStatus::Unread) mail_.unread = true;
}

void GameConnection::mail_delete(const std::vector<std::uint32_t>& ids) {
    // CMailWnd's Delete: from the inbox to the trash (TRASH); from the trash for good (DELETED).
    std::vector<std::uint32_t> to_trash, to_delete;
    for (auto id : ids) {
        auto it = mail_.headers.find(id);
        if (it == mail_.headers.end()) continue;
        (it->second.status == game::MailStatus::Trash ? to_delete : to_trash).push_back(id);
    }
    mail_set_status(to_trash, game::MailStatus::Trash);
    mail_set_status(to_delete, game::MailStatus::Deleted);
}

void GameConnection::mail_undelete(const std::vector<std::uint32_t>& ids) { mail_set_status(ids, game::MailStatus::Read); }

void GameConnection::mail_empty_trash() {
    // The Purge button: every trash message DELETED.
    std::vector<std::uint32_t> ids;
    for (const auto& [id, h] : mail_.headers)
        if (h.status == game::MailStatus::Trash) ids.push_back(id);
    mail_set_status(ids, game::MailStatus::Deleted);
}

bool GameConnection::mail_send(const std::string& to, const std::string& subject, const std::string& body) {
    // CMailCompositionWnd's Send → MailMgr_SendMail @0x46be70.
    if (to.empty()) return false;
    auto lists = mail_builtin_lists();
    return mail_send_command(game::mail_to_command(to, subject, body, mail_server(), &mail_.address_book, &lists));
}

std::vector<game::MailAddressEntry> GameConnection::mail_builtin_lists() const {
    // The original's address book has "Guild" and group entries of its own (docs/unknowns.md "Mail"); how it fills
    // them is not decoded, so these are the other members this client knows.
    std::vector<game::MailAddressEntry> out;
    auto other = [&](const std::string& n) { return !n.empty() && !iequals(n, character_); };
    game::MailAddressEntry guild{"Guild", {}, false, {}};
    for (const auto& m : guild_.members)
        if (other(m.name)) guild.addresses.push_back(m.name);
    if (!guild.addresses.empty()) out.push_back(std::move(guild));
    game::MailAddressEntry group{"Group", {}, false, {}};
    for (const auto& m : group_.slots)
        if (other(m.name)) group.addresses.push_back(m.name);
    if (!group.addresses.empty()) out.push_back(std::move(group));
    return out;
}

void GameConnection::handle_mail_event(const ChatServerEvent& e) {
    using K = ChatServerEvent::Kind;
    const auto& f = e.fields;
    auto box_name = [&](int n) {
        return n >= 0 && n < static_cast<int>(mail_.mailboxes.size()) ? game::mail_short_name(mail_.mailboxes[static_cast<std::size_t>(n)])
                                                                         : std::to_string(n);
    };
    switch (e.kind) {
    case K::MailboxList:
        // MailMgr_OnMailboxList @0x46d930. The original tells the chat service the forwarding setting when its
        // options window syncs the mail page (COptionsWnd::sub_00700dc0, at the UI's creation, when the chat
        // service is there); here when the mail login is answered, the first moment the chat service takes commands.
        mail_.mailboxes = f;
        log::info("mail: {} mailbox(es){}{}", f.size(), f.empty() ? "" : ", the first ", f.empty() ? "" : f[0]);
        mail_set_forwarding(client_option(game::ClientOption::MailForwarding));
        break;
    case K::MailboxChange: {
        // FUN_00521210: "You switched to mailbox %1." (4534) and the headers are fetched.
        int n = f.empty() ? -1 : std::atoi(f[0].c_str());
        if (n < 0) {
            system(4535, {f.empty() ? std::string() : f[0]});
            break;
        }
        mail_.current = n;
        system(4534, {box_name(n)});
        mail_request_headers();
        break;
    }
    case K::MailHeaderCount: {
        // The header count (0x5213f0..): 4538 / 4539 / 4540 with the mailbox name, 4541 on failure.
        if (f.size() < 4) break;
        int box = std::atoi(f[0].c_str());
        if (box != mail_.current) break;
        bool ok = f[2] != "0";
        auto count = static_cast<std::int32_t>(to_u32(f[3]));
        if (!ok) {
            system(4541, {box_name(box)});
            break;
        }
        mail_.expected = count;
        if (count == 0) system(4538, {box_name(box)});
        else if (count == 1) system(4539, {box_name(box)});
        else system(4540, {box_name(box), std::to_string(count)});
        break;
    }
    case K::MailHeader: {
        // FUN_00533650 → MailMgr_OnHeader @0x4698b0 (the current mailbox only).
        if (f.size() < 8 || std::atoi(f[0].c_str()) != mail_.current) break;
        game::MailHeader h;
        h.id = to_u32(f[3]);
        h.timestamp = to_u32(f[4]);
        h.status = game::mail_status_from_server(std::atoi(f[5].c_str()));
        h.from = f[6];
        h.subject = f[7];
        if (!is_ignored(game::mail_short_name(h.from))) mail_.store(std::move(h));
        break;
    }
    case K::MailNew: {
        // FUN_00533780 → MailMgr_OnNewMail @0x469a30: another mailbox gets 4533 ("You received a new message in
        // your primary mailbox.", colour 15) and nothing else; the current one 4547 ("New mail from %1: \"%2\".",
        // colour 10), the "YouveGotMail" sound and the header. The lines need NewMailNotificationInChatWindow
        // (MailMgr +0xe0, set by FUN_004688f0 from the options' mail page), the sound NewMailAudioNotification
        // (+0xe1, FUN_00468900).
        if (f.size() < 6) break;
        int box = std::atoi(f[0].c_str());
        bool notify = client_option(game::ClientOption::NewMailNotify);
        if (box != mail_.current) {
            if (notify) system(4533, {}, 15);
            break;
        }
        game::MailHeader h;
        h.id = to_u32(f[1]);
        h.timestamp = to_u32(f[2]);
        h.status = game::mail_status_from_server(std::atoi(f[3].c_str()));
        h.from = f[4];
        h.subject = f[5];
        if (notify) system(4547, {game::mail_short_name(h.from), h.subject}, 10);
        if (client_option(game::ClientOption::NewMailSound)) {
            SoundCue cue;
            cue.kind = SoundCue::Kind::NewMail;
            sound_cues_.push_back(cue);
        }
        if (!is_ignored(game::mail_short_name(h.from))) mail_.store(std::move(h));
        break;
    }
    case K::MailBody: {
        // FUN_0051a600 → MailMgr_OnMessageBody @0x46db20.
        if (f.size() < 3 || std::atoi(f[0].c_str()) != mail_.current) break;
        bool ok = f.size() < 4 || f[3] != "0";
        if (!ok) {
            log::warn("Mail System: could not fetch message body.");
            break;
        }
        auto it = mail_.headers.find(to_u32(f[1]));
        if (it == mail_.headers.end()) break;
        it->second.body = f[2];
        std::string to;
        for (std::size_t i = 4; i < f.size(); ++i) to += f[i];
        while (!to.empty() && (to.front() == '\n' || to.front() == ' ')) to.erase(to.begin());
        if (to.rfind("TO:", 0) == 0) to = to.substr(3);
        while (!to.empty() && to.back() == '\n') to.pop_back();
        it->second.to = to;
        break;
    }
    case K::MailDelivery: {
        // FUN_00521180: 4530 "Message sent." or 4531 "Mail delivery failure to %1 (subject: %2)".
        if (f.size() < 4) break;
        if (f[3] == "1") {
            system(4530);
            mail_.delivery.push_back(strings_.format(4530, std::vector<std::string>{}));
        } else {
            std::string to = f[1];
            while (!to.empty() && to.front() == ' ') to.erase(to.begin());
            system(4531, {to, f[2]});
            mail_.delivery.push_back(strings_.format(4531, std::vector<std::string>{to, f[2]}));
        }
        if (mail_.delivery.size() > 20) mail_.delivery.erase(mail_.delivery.begin());
        break;
    }
    default:
        break;
    }
}

} // namespace nmsc::net
