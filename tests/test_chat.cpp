// The chat box's lines (chat_hook.cpp): every player line BfMenu::addPlayerChatMessage (0x004CC2A0) puts in
// the chat box, the recording player's own among them, written whole as BF42++ writes them
// (replay.cpp replay_onChat) and as the server recorder writes its own (core.c write_chat_locked): the
// server never sends a player his own line back, so the ChatFragment pieces alone missed everything he
// said (the owner, 2026-11-23).
#include <cstdint>
#include <string>
#include <vector>
#include "check.h"
#include "replay_detail.h"
#include "replay_format.h"

TEST(chat_text_is_the_line_as_latin1)
{
    // Units below 256 as themselves; U+20AC as the 0x80 byte the server's lines put between words (a
    // Windows ANSI line widened on the way to the chat box); anything else a question mark.
    const uint16_t units[] = { 'H', 'i', ' ', 0xE9, 0x20AC, 'x', 0x4E2D };
    CHECK_EQ(rf::chatText(units, 7), std::string("Hi \xE9\x80x?"));
}

TEST(chat_text_stops_at_a_nul_and_at_255)
{
    const uint16_t units[] = { 'o', 'k', 0, 'z' };
    CHECK_EQ(rf::chatText(units, 4), std::string("ok"));
    std::vector<uint16_t> longLine(400, 'a');
    CHECK_EQ(rf::chatText(longLine.data(), longLine.size()).size(), (size_t)255);
    CHECK_EQ(rf::chatText(nullptr, 3), std::string());
}

TEST(chat_line_is_bf42plus_record)
{
    CHECK_EQ(rf::chatLine(12.3456, 5, 1, "Pilot Joe: on me"),
        std::string("{\"k\":\"chat\",\"t\":12.346,\"pid\":5,\"team\":1,\"text\":\"Pilot Joe: on me\"}"));
    // The server's own line: no sender.
    CHECK_EQ(rf::chatLine(3, -1, 0, "*Welcome"), std::string("{\"k\":\"chat\",\"t\":3.000,\"pid\":-1,\"team\":0,\"text\":\"*Welcome\"}"));
}

TEST(chat_line_escapes_its_text)
{
    CHECK_EQ(rf::chatLine(1, 2, 2, "Caf\xE9 \"A\"\\B\x80" "c"),
        std::string("{\"k\":\"chat\",\"t\":1.000,\"pid\":2,\"team\":2,\"text\":\"Caf\xC3\xA9 \\\"A\\\"\\\\B\xC2\x80" "c\"}"));
}

TEST(header_says_when_it_has_the_chat_boxs_lines)
{
    rf::Header plain{ "0.3-dev", "bfvietnam", "Nov 23 2026 20:00:00", "2026-11-23T20:00:00", 30 };
    CHECK_EQ(rf::headerLine(plain),
        std::string("{\"k\":\"h\",\"v\":5,\"plus\":\"bfvplus/0.3-dev\",\"game\":\"bfv\",\"mod\":\"bfvietnam\",\"build\":\"Nov 23 2026 20:00:00\",\"start\":\"2026-11-23T20:00:00\",\"hz\":30}"));
    rf::Header withChat = plain;
    withChat.chatLines = true;
    CHECK_EQ(rf::headerLine(withChat),
        std::string("{\"k\":\"h\",\"v\":5,\"plus\":\"bfvplus/0.3-dev\",\"game\":\"bfv\",\"mod\":\"bfvietnam\",\"build\":\"Nov 23 2026 20:00:00\",\"start\":\"2026-11-23T20:00:00\",\"hz\":30,\"chatLines\":1}"));
}

TEST(chat_queue_keeps_order_and_forgets_on_clear)
{
    ChatQueue q(2);
    q.push(10, 3, 2, "a");
    q.push(20, -1, 0, "b");
    q.push(30, 4, 1, "c");   // past the cap: dropped and counted
    std::vector<QueuedChat> out;
    q.drain(out);
    CHECK_EQ(out.size(), (size_t)2);
    CHECK_EQ(out[0].ms, (uint64_t)10);
    CHECK_EQ(out[0].pid, 3);
    CHECK_EQ(out[0].team, 2);
    CHECK_EQ(out[0].text, std::string("a"));
    CHECK_EQ(out[1].text, std::string("b"));
    CHECK_EQ(q.dropped(), (size_t)1);
    q.drain(out);
    CHECK_EQ(out.size(), (size_t)0);
    q.push(40, 1, 1, "d");
    q.clear();
    q.drain(out);
    CHECK_EQ(out.size(), (size_t)0);
    CHECK_EQ(q.dropped(), (size_t)0);
}
