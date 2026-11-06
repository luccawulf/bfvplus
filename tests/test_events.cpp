// Event records: each client event payload from the lab session must give exactly the line the lab
// server recorder wrote for the same event (tests/fixtures/events.txt, Session A 2026-11-06).
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "check.h"
#include "replay_events.h"

static std::vector<uint8_t> unhex(const std::string& h)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back((uint8_t)std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}

// Template names the server resolved: the expected line itself says which id has which name.
static std::map<uint32_t, std::string> namesIn(const std::string& line)
{
    std::map<uint32_t, std::string> m;
    std::smatch a, b;
    if (std::regex_search(line, a, std::regex("\"tid\":(\\d+)")) && std::regex_search(line, b, std::regex("\"tmpl\":\"([^\"]*)\"")))
        m[(uint32_t)std::stoul(a[1])] = b[1];
    if (std::regex_search(line, a, std::regex("\"weapon\":(-?\\d+)")) && std::regex_search(line, b, std::regex("\"weaponName\":\"([^\"]*)\"")))
        m[(uint32_t)std::stol(a[1])] = b[1];
    return m;
}

TEST(events_match_server)
{
    std::ifstream f("tests/fixtures/events.txt");
    CHECK(f.good());
    int n = 0, bad = 0;
    for (std::string l; std::getline(f, l);) {
        if (l.empty() || l[0] == '#') continue;
        size_t bar = l.find(" | ");
        std::istringstream head(l.substr(0, bar));
        std::string typeHex, payloadHex;
        head >> typeHex >> payloadHex;
        std::string want = l.substr(bar + 3);
        auto names = namesIn(want);
        EventDecoder dec([&](uint32_t id) { auto it = names.find(id); return it == names.end() ? std::string() : it->second; });
        std::vector<uint8_t> p = unhex(payloadHex);
        std::string got;
        bool ok = dec.line(std::stoi(typeHex, nullptr, 16), p.data(), p.size(), 0.0, got);
        ++n;
        if (!ok || got != want) {
            if (++bad <= 5) check::fail(__FILE__, __LINE__, "type " + typeHex + "\n    got:  " + (ok ? got : "(none)") + "\n    want: " + want);
        }
    }
    CHECK(n >= 90);
    CHECK_EQ(bad, 0);
}

TEST(control_said_once)
{
    EventDecoder dec;
    const uint8_t p[] = { 3, 0x10, 0x02 };   // pid 3, net id 528
    std::string a, b, c;
    CHECK(dec.line(0x09, p, sizeof(p), 1.0, a));
    CHECK(!dec.line(0x09, p, sizeof(p), 2.0, b));
    const uint8_t q[] = { 3, 0x11, 0x02 };
    CHECK(dec.line(0x09, q, sizeof(q), 3.0, c));
    CHECK_EQ(a, std::string("{\"k\":\"e\",\"t\":1.000,\"e\":\"control\",\"pid\":3,\"netId\":528}"));
}

TEST(short_payload_reads_zeros)
{
    // Past the end of what the caller copied counts as zero, never a read beyond it.
    EventDecoder dec;
    const uint8_t p[] = { 7 };
    std::string out;
    CHECK(dec.line(0x0a, p, sizeof(p), 0.0, out));
    CHECK_EQ(out, std::string("{\"k\":\"e\",\"t\":0.000,\"e\":\"enterVehicle\",\"pid\":7,\"netId\":0}"));
}

TEST(server_style_escape)
{
    // serverName: char[32]; high and control bytes as \u00XX, as the server recorder writes them.
    EventDecoder dec;
    uint8_t p[33] = {};
    const char name[] = "Caf\xE9 \"1\"\n";
    for (size_t i = 0; i < sizeof(name) - 1; ++i) p[i] = (uint8_t)name[i];
    std::string out;
    CHECK(dec.line(0x1b, p, sizeof(p), 0.0, out));
    CHECK_EQ(out, std::string("{\"k\":\"e\",\"t\":0.000,\"e\":\"serverName\",\"name\":\"Caf\\u00e9 \\\"1\\\"\\u000a\"}"));
}

TEST(server_info_is_0x1a)
{
    CHECK(isServerInfo(0x1a));
    CHECK(!isServerInfo(0x1b));
}

TEST(unknown_type_not_written)
{
    EventDecoder dec;
    const uint8_t p[8] = {};
    std::string out;
    CHECK(!dec.line(0x27, p, sizeof(p), 0.0, out));
    CHECK(!dec.line(0x3c, p, sizeof(p), 0.0, out));
}
