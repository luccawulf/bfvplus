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

// ---- stages 2-3 -----------------------------------------------------------------------------
std::unordered_map<uintptr_t, std::string> g_tmplName;   // template -> name, for parts and shots

const std::string& cachedTemplateName(uintptr_t tmpl)
{
    auto it = g_tmplName.find(tmpl);
    if (it != g_tmplName.end()) return it->second;
    return g_tmplName[tmpl] = seh::ptrSane(tmpl) ? templateName(tmpl) : std::string();
}

uintptr_t rootOf(uintptr_t obj)
{
    uintptr_t r = obj;
    for (int d = 0; d < 16; ++d) {
        uintptr_t p = seh::u32(r + bfv::OBJ_PARENT);
        if (!seh::ptrSane(p)) break;
        r = p;
    }
    return r;
}

// An object's Armor: the value under ARMOR_IID in its component map (an MSVC std::map, head node at
// obj+0xDC, walked in order from the leftmost as the probe did; objects hold a handful of components).
uintptr_t armorOf(uintptr_t obj)
{
    uintptr_t head = seh::u32(obj + bfv::COMP_MAP_HEAD);
    if (!seh::ptrSane(head)) return 0;
    uintptr_t node = seh::u32(head + 0);
    for (int n = 0; n < 16 && node != head && seh::ptrSane(node); ++n) {
        if (seh::u32(node + bfv::COMP_NODE_KEY) == bfv::ARMOR_IID) {
            uintptr_t v = seh::u32(node + bfv::COMP_NODE_VALUE);
            return seh::ptrSane(v) && seh::u32(v) == VT_Armor.va ? v : 0;
        }
        node = treeSucc(node, head);
    }
    return 0;
}

struct RootInfo { int net; bool vehicle; float rows[16]; };

void readArmorInto(uintptr_t obj, LiveObject& o)
{
    uintptr_t a = armorOf(obj);
    uint8_t b[bfv::ARMOR_DESTROYED + 1];
    ArmorFields f{};
    if (!a || !seh::read(a, b, sizeof(b)) || !decodeArmor(b, sizeof(b), f)) return;
    o.hasArmor = true;
    o.hp = f.hp;
    o.maxhp = f.maxhp;
    o.crit = f.crit;
    o.lastHit = f.lastHit;
}

// The children and other classes the walk set aside, resolved against the sampled roots.
void readDetail(const std::vector<std::pair<uintptr_t, uintptr_t>>& pending,
    const std::unordered_map<uintptr_t, RootInfo>& roots, DetailRead& d)
{
    for (const auto& pv : pending) {
        const uintptr_t obj = pv.first, vt = pv.second;
        if (vt == VT_ControlPoint.va) {
            LiveControlPoint cp{};
            uint8_t b[bfv::CP_TEAM + 4];
            if (!seh::read(obj, b, sizeof(b)) || !decodeCpTeam(b, sizeof(b), cp.team)) continue;
            uintptr_t tmpl = seh::u32(obj + bfv::OBJ_TEMPLATE);
            cp.id = (int)seh::u32(obj + bfv::OBJ_ID);
            cp.tmpl = cachedTemplateName(tmpl);
            cp.name = seh::ptrSane(tmpl) ? seh::msvcString(tmpl + bfv::CPT_NAME) : std::string();
            float p[3];
            cp.hasPos = seh::read(obj + bfv::OBJ_POS, p, sizeof(p)) && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
            cp.x = cp.hasPos ? p[0] : 0; cp.y = cp.hasPos ? p[1] : 0; cp.z = cp.hasPos ? p[2] : 0;
            d.cps.push_back(cp);
            continue;
        }
        auto r = roots.find(rootOf(obj));
        if (r == roots.end()) continue;
        const RootInfo& root = r->second;
        const uintptr_t tmpl = seh::u32(obj + bfv::OBJ_TEMPLATE);
        if (vt == VT_RotationalBundle.va) {
            LivePart p{};
            if (!seh::read(obj + bfv::OBJ_ROWS, p.rows, sizeof(p.rows))) continue;
            p.key = obj; p.tmplKey = tmpl; p.rootNetId = root.net; p.name = cachedTemplateName(tmpl);
            std::memcpy(p.rootRows, root.rows, sizeof(p.rootRows));
            d.parts.push_back(p);
        } else if (vt == VT_Engine.va) {
            uintptr_t pe = seh::u32(obj + bfv::ENG_PHYS);
            uint8_t eb[bfv::ENG_DISABLED + 1], pb[bfv::PE_GEAR + 4];
            EngineFields f{};
            if (!seh::ptrSane(pe) || seh::u32(pe) != VT_PhysicsEngine.va || !seh::read(obj, eb, sizeof(eb))
                || !seh::read(pe, pb, sizeof(pb)) || !decodeEngine(eb, sizeof(eb), pb, sizeof(pb), f))
                continue;
            d.engines.push_back(LiveEngine{ obj, tmpl, root.net, f.revs, f.throttle, f.flags, f.gear });
        } else if (vt == VT_MusicPlayer.va) {
            uint8_t b[bfv::MUS_LEFT + 4];
            MusicFields f{};
            if (!seh::read(obj, b, sizeof(b)) || !decodeMusic(b, sizeof(b), f)) continue;
            d.music.push_back(LiveMusic{ obj, root.net, f.playing, f.song, f.group, f.left });
        }
    }
}

}  // namespace

void templateCacheReset()
{
    g_idToName.clear();
    g_tmplToId.clear();
    g_tmplName.clear();
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

int readObjects(std::vector<LiveObject>& out, ReadStats* stats, DetailRead* detail)
{
    out.clear();
    if (detail) *detail = DetailRead{};
    std::vector<std::pair<uintptr_t, uintptr_t>> pending;   // (object, vtable) for readDetail
    std::unordered_map<uintptr_t, RootInfo> roots;
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
        if (detail && (vt == VT_RotationalBundle.va || vt == VT_Engine.va || vt == VT_MusicPlayer.va || vt == VT_ControlPoint.va)) {
            pending.emplace_back(obj, vt);
            continue;
        }
        // Rounds in flight (replay-recorder-2), as the server recorder takes them: an enabled root whose
        // vtable is exactly Projectile's. Rockets, bombs and missiles (the SA-7 included) fly on every
        // client as their own objects; a pooled round sits disabled between flights.
        if (vt == VT_Projectile.va) {
            if (!detail || detail->projectiles.size() >= 1024) continue;
            const uint32_t flags = seh::u32(obj + bfv::OBJ_FLAGS, &ok);
            if (!ok || (flags & bfv::OBJ_FLAG_DISABLED) || seh::u32(obj + bfv::OBJ_PARENT)) continue;
            float p[3];
            if (!seh::read(obj + bfv::OBJ_POS, p, sizeof(p))) continue;
            uintptr_t tmpl = seh::u32(obj + bfv::OBJ_TEMPLATE);
            detail->projectiles.push_back(LiveProjectile{ (int)seh::u32(obj + bfv::OBJ_ID),
                seh::ptrSane(tmpl) ? templateName(tmpl) : std::string(), p[0], p[1], p[2] });
            continue;
        }
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
        if (detail) {
            readArmorInto(obj, o);
            RootInfo& ri = roots[obj];
            ri.net = net;
            ri.vehicle = vt == VT_PCO;
            std::memcpy(ri.rows, o.rows, sizeof(ri.rows));
            uint8_t sb[bfv::SOL_BITS + 2];
            LiveSoldier ls{};
            ls.netId = net;
            if (vt == VT_SOLDIER && seh::read(obj, sb, sizeof(sb)) && decodeSoldier(sb, sizeof(sb), ls.f))
                detail->soldiers.push_back(ls);
        }
        out.push_back(o);
        if (vt == VT_SOLDIER) st.soldiers++; else st.vehicles++;
    }
    if (detail) readDetail(pending, roots, *detail);
    if (stats) *stats = st;
    return (int)out.size();
}

bool readTickets(LiveTickets& out)
{
    out = LiveTickets{ false, 0, 0 };
    uintptr_t sm = seh::u32(bfv::SCOREMGR_GLOBAL);
    const size_t n = bfv::SCORE_TEAM_BASE + 2 * bfv::SCORE_TEAM_STRIDE + bfv::SCORE_TICKETS + 4;
    uint8_t b[n];
    if (!seh::ptrSane(sm) || seh::u32(sm) != VT_ScoreManager.va || !seh::read(sm, b, n)) return false;
    out.valid = decodeTickets(b, n, out.team1, out.team2);
    return out.valid;
}

bool readHitMark(float& v, int& pid, uintptr_t& bfPlayer)
{
    v = 0;
    pid = -1;
    bfPlayer = 0;
    uintptr_t pm = playerManager(), local = 0;
    if (!pm || !seh::vcall0(pm, bfv::VSLOT_LOCAL_PLAYER, &local) || !seh::ptrSane(local)) return false;
    bfPlayer = local;
    bool ok;
    pid = (int)seh::u32(local + bfv::PLAYER_ID, &ok);
    return ok && seh::read(local + bfv::PLAYER_HITMARK, &v, sizeof(v));
}

bool readAnimTable(AnimTable& out)
{
    out.states.clear();
    out.total = 0;
    uintptr_t m = seh::u32(bfv::ANIM_ASM_GLOBAL);
    if (!seh::ptrSane(m)) return false;
    uintptr_t b = seh::u32(m + bfv::ASM_STATES), e = seh::u32(m + bfv::ASM_STATES + 4);
    if (!seh::ptrSane(b) || e <= b || (e - b) % 4) return false;
    // The manager has no vtable to check (its first dword is a heap pointer), so ask for a table that
    // looks like one: the first and last entries point at states, and the first state has a name.
    const uintptr_t first = seh::u32(b), last = seh::u32(e - 4);
    if (!seh::ptrSane(first) || !seh::ptrSane(last) || seh::msvcString(first + bfv::STATE_NAME).empty()) return false;
    out.total = (e - b) / 4;
    if (out.total > kAnimStatesMax) return true;   // the tracker writes BF42++'s "skipped" line
    out.states.reserve(out.total);
    for (size_t i = 0; i < out.total; ++i) {
        uintptr_t s = seh::u32(b + 4 * i);
        if (!seh::ptrSane(s)) continue;
        out.states.push_back(rf::AnimState{ (int)i, seh::msvcString(s + bfv::STATE_NAME), seh::u32(s + bfv::STATE_FLAGS) });
    }
    return true;
}

bool readShot(uintptr_t fireArms, uintptr_t iplayer, uintptr_t mat, LiveShot& out, uintptr_t& bfPlayer)
{
    out = LiveShot{};
    bfPlayer = 0;
    if (!seh::read(mat, out.m, sizeof(out.m))) return false;
    uintptr_t vt = seh::ptrSane(iplayer) ? seh::u32(iplayer) : 0;
    if (vt == VT_PLAYER) bfPlayer = iplayer;
    else if (vt == VT_PLAYER_4) bfPlayer = iplayer - 4;
    else if (vt == VT_PLAYER_8) bfPlayer = iplayer - 8;
    bool ok = false;
    out.pid = bfPlayer ? (int)seh::u32(bfPlayer + bfv::PLAYER_ID, &ok) : -1;
    if (bfPlayer && !ok) out.pid = -1;
    out.rootNetId = netIdOf(rootOf(fireArms));
    uintptr_t tmpl = seh::u32(fireArms + bfv::OBJ_TEMPLATE);
    out.weapon = seh::ptrSane(tmpl) ? templateName(tmpl) : std::string();   // not the cache: another thread may sample
    return true;
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
