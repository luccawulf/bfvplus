// Developer tool (not part of the tests): read `<type> <payload hex>` lines on stdin and print each
// event's record (t = 0) or "-" when the type isn't written. Used with the lab probe session to keep
// only fixtures whose decoded line the lab server recorder wrote verbatim.
// Build: g++ -std=c++17 -Isrc tests/tools/decode_events.cpp src/replay_events.cpp -o /tmp/decode_events
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "replay_events.h"

int main()
{
    for (std::string l; std::getline(std::cin, l);) {
        std::istringstream in(l);
        int type;
        std::string hex;
        in >> type >> hex;
        std::vector<uint8_t> p;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) p.push_back((uint8_t)std::stoi(hex.substr(i, 2), nullptr, 16));
        EventDecoder dec;   // fresh per line: layout only, no repeat suppression
        std::string out;
        std::cout << (dec.line(type, p.data(), p.size(), 0.0, out) ? out : std::string("-")) << "\n";
    }
}
