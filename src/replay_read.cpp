// The replay recorder's reads of the live game (see replay_read.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <unordered_map>
#include "bfv_addresses.h"
#include "replay_read.h"
#include "seh_read.h"

namespace {

const uintptr_t VT_PCO = VT_PlayerControlObject.va;   // 0x00B93D24
const uintptr_t VT_SOLDIER = VT_BFSoldier.va;         // 0x00B9033C
const uintptr_t VT_PLAYER = VT_BFPlayer.va;           // 0x00B5F70C (subobject +0)
const uintptr_t VT_PLAYER_4 = 0x00B5F6F4;             // BFPlayer's subobject +4
const uintptr_t VT_PLAYER_8 = 0x00B5F6C4;             // and +8
const uintptr_t TEMPLATE_MGR_GLOBAL = 0x00D7D018;     // capture.cpp: tree header at mgr+0x0C

int netIdOf(uintptr_t obj)
{
    uintptr_t net = seh::u32(obj + bfv::OBJ_NETWORKABLE);
    if (!seh::ptrSane(net)) return -1;
    bool ok;
    uint16_t id = seh::u16(net + bfv::NET_ID, &ok);
    return ok ? id : -1;
}

// ---- templates: id <-> name, from the ObjectTemplateManager's tree --------------------------
std::unordered_map<uint32_t, std::string> g_idToName;
std::unordered_map<uintptr_t, uint32_t> g_tmplToId;
DWORD g_lastTemplateWalk = 0;
bool g_walkedOnce = false;

uintptr_t treeSucc(uintptr_t node, uintptr_t header)
{
    uintptr_t right = seh::u32(node + 8);
    if (right != header) {
        node = right;
        for (int d = 0; d < 64; ++d) {
            uintptr_t l = seh::u32(node + 0);
            if (l == header || !seh::ptrSane(l)) break;
            node = l;
        }
        return node;
    }
    uintptr_t p = seh::u32(node + 4);
    for (int d = 0; d < 64 && p != header && seh::ptrSane(p) && node == seh::u32(p + 8); ++d) {
        node = p;
        p = seh::u32(p + 4);
    }
    return seh::ptrSane(p) ? p : header;
}

std::string templateName(uintptr_t tmpl)
{
    std::string s = seh::msvcString(tmpl + bfv::TMPL_NAME);
    return s.empty() ? seh::msvcString(tmpl + 0x2C) : s;   // a few classes keep it at +0x2C (capture.cpp)
}

void walkTemplates()
{
    DWORD now = GetTickCount();
    if (g_walkedOnce && now - g_lastTemplateWalk < 5000) return;
    g_walkedOnce = true;
    g_lastTemplateWalk = now;
    uintptr_t mgr = seh::u32(TEMPLATE_MGR_GLOBAL);
    uintptr_t header = seh::ptrSane(mgr) ? seh::u32(mgr + 0x0C) : 0;
    if (!seh::ptrSane(header)) return;
    g_idToName.clear();
    g_tmplToId.clear();
    uintptr_t node = seh::u32(header + 0);
    for (int guard = 0; node != header && seh::ptrSane(node) && guard < 40000; ++guard) {
        uint32_t id = seh::u32(node + 0x0C);
        uintptr_t tmpl = seh::u32(node + 0x10);
        if (seh::ptrSane(tmpl)) {
            g_idToName[id] = templateName(tmpl);
            g_tmplToId[tmpl] = id;
        }
        node = treeSucc(node, header);
    }
}

// The player manager, only when the global points at one (a stale or half-built pointer would make
// the virtual call below jump anywhere).
uintptr_t playerManager()
{
    uintptr_t pm = seh::u32(bfv::PLAYERMGR_GLOBAL);
    return seh::ptrSane(pm) && seh::u32(pm) == VT_PlayerManager.va ? pm : 0;
}

}  // namespace

void templateCacheReset()
{
    g_idToName.clear();
    g_tmplToId.clear();
    g_walkedOnce = false;
}

std::string templateNameById(uint32_t id)
{
    auto it = g_idToName.find(id);
    if (it != g_idToName.end()) return it->second;
    walkTemplates();
    it = g_idToName.find(id);
    return it == g_idToName.end() ? std::string() : it->second;
}

int readObjects(std::vector<LiveObject>& out, ReadStats* stats)
{
    out.clear();
    ReadStats st;
    bool ok;
    uintptr_t mgr = seh::u32(bfv::OBJMGR_GLOBAL, &ok);
    if (!ok || !seh::ptrSane(mgr)) return -1;
    uintptr_t head = seh::u32(mgr + bfv::OBJMGR_LIST_HEAD, &ok);
    if (!ok || !seh::ptrSane(head)) return -1;
    st.listSize = seh::u32(mgr + bfv::OBJMGR_LIST_SIZE);
    uintptr_t node = seh::u32(head + bfv::LIST_NEXT);
    const unsigned guardMax = st.listSize + 64 < 60000 ? st.listSize + 64 : 60000;
    for (unsigned guard = 0; node != head && seh::ptrSane(node) && guard < guardMax; ++guard) {
        uintptr_t obj = seh::u32(node + bfv::LIST_OBJECT);
        node = seh::u32(node + bfv::LIST_NEXT);
        st.walked++;
        uintptr_t vt = seh::u32(obj);
        if (vt != VT_PCO && vt != VT_SOLDIER) continue;
        if (seh::u32(obj + bfv::OBJ_PARENT)) continue;          // a seat or part: the root carries it
        int net = netIdOf(obj);
        if (net < 0) continue;
        LiveObject o{};
        if (!seh::read(obj + bfv::OBJ_ROWS, o.rows, sizeof(o.rows))) continue;
        if (std::fabs(o.rows[12]) < 2.0f && std::fabs(o.rows[14]) < 2.0f) continue;   // a pooled copy at the origin
        uintptr_t tmpl = seh::u32(obj + bfv::OBJ_TEMPLATE);
        o.netId = net;
        o.gid = (int)seh::u32(obj + bfv::OBJ_ID);
        o.tmpl = seh::ptrSane(tmpl) ? templateName(tmpl) : std::string();
        auto it = g_tmplToId.find(tmpl);
        if (it == g_tmplToId.end()) { walkTemplates(); it = g_tmplToId.find(tmpl); }
        o.tid = it == g_tmplToId.end() ? 0 : (int)it->second;
        o.team = 0;
        out.push_back(o);
        if (vt == VT_SOLDIER) st.soldiers++; else st.vehicles++;
    }
    if (stats) *stats = st;
    return (int)out.size();
}

int readPlayers(std::vector<LivePlayer>& out)
{
    out.clear();
    uintptr_t pm = playerManager();
    if (!pm) return 0;
    uintptr_t local = 0;
    seh::vcall0(pm, bfv::VSLOT_LOCAL_PLAYER, &local);
    uintptr_t head = seh::u32(pm + bfv::PM_LIST_HEAD);
    if (!seh::ptrSane(head)) return 0;
    uintptr_t node = seh::u32(head);
    for (int guard = 0; node != head && seh::ptrSane(node) && guard < 300; ++guard) {
        uintptr_t pl = seh::u32(node + bfv::PM_NODE_PLAYER);
        node = seh::u32(node);
        uintptr_t vt = seh::u32(pl);
        if (vt == VT_PLAYER_4) pl -= 4;           // the list may hold an interface pointer
        else if (vt == VT_PLAYER_8) pl -= 8;
        else if (vt != VT_PLAYER) continue;
        LivePlayer p{};
        p.pid = (int)seh::u32(pl + bfv::PLAYER_ID);
        p.team = (int)seh::u32(pl + bfv::PLAYER_TEAM);
        uint8_t ai = 0;
        seh::read(pl + bfv::PLAYER_AI, &ai, 1);
        p.ai = ai != 0;
        p.name = seh::msvcString(pl + bfv::PLAYER_NAME);
        p.local = pl == local;
        uintptr_t ctrl = 0;
        seh::vcall0(pl, bfv::VSLOT_PLAYER_VEHICLE, &ctrl);
        p.ctrlNetId = seh::ptrSane(ctrl) ? netIdOf(ctrl) : -1;
        uintptr_t root = ctrl;
        for (int d = 0; d < 8 && seh::ptrSane(root); ++d) {
            uintptr_t parent = seh::u32(root + bfv::OBJ_PARENT);
            if (!seh::ptrSane(parent)) break;
            root = parent;
        }
        p.rootNetId = seh::ptrSane(root) ? netIdOf(root) : -1;
        p.seat = p.ctrlNetId >= 0 && p.rootNetId >= 0 && p.ctrlNetId >= p.rootNetId ? p.ctrlNetId - p.rootNetId : 0;
        out.push_back(p);
    }
    return (int)out.size();
}

bool localPlayerPresent()
{
    uintptr_t pm = playerManager();
    uintptr_t local = 0;
    return pm && seh::vcall0(pm, bfv::VSLOT_LOCAL_PLAYER, &local) && local != 0;
}
