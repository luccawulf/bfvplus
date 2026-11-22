// What changed since the last sample (see replay_track.h).
#include "replay_track.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include "replay_pose.h"

namespace {

bool moved(const rf::Pose& a, const rf::Pose& b)
{
    return std::fabs(a.x - b.x) > 0.01f || std::fabs(a.y - b.y) > 0.01f || std::fabs(a.z - b.z) > 0.01f;
}

bool turned(const rf::Pose& a, const rf::Pose& b)
{
    return std::fabs(a.qx - b.qx) > 0.002f || std::fabs(a.qy - b.qy) > 0.002f || std::fabs(a.qz - b.qz) > 0.002f
        || std::fabs(a.qw - b.qw) > 0.002f;
}

bool sameRow(const rf::PlayerRow& a, const rf::PlayerRow& b)
{
    return a.pid == b.pid && a.team == b.team && a.vehNetId == b.vehNetId && a.rootNetId == b.rootNetId
        && a.seat == b.seat && a.triggers == b.triggers;
}

}  // namespace

void ObjectTracker::sample(double t, const std::vector<LiveObject>& objects, std::vector<std::string>& lines)
{
    std::vector<std::string> dead, born;
    std::vector<rf::Pose> rows;
    std::vector<rf::ArmorRow> armor;
    // Stage 2: an a row when the hit points are numbers and moved over 0.05, or the last hitter changed.
    auto noteArmor = [&armor](const LiveObject& o, Known& k) {
        if (!o.hasArmor || !std::isfinite(o.hp)) return;
        if (k.hasArmor && std::fabs(o.hp - k.hp) <= 0.05f && o.lastHit == k.lastHit) return;
        k.hasArmor = true;
        k.hp = o.hp;
        k.lastHit = o.lastHit;
        armor.push_back(rf::ArmorRow{ o.netId, o.hp, o.lastHit });
    };
    std::set<int> seen;
    for (const LiveObject& o : objects) {
        rf::Pose p = poseFromBody(o.netId, o.rows);
        if (!rf::finitePose(p)) {
            // Skipped for this sample only: a known object stays known (no d, no fresh o).
            auto k = known_.find(o.netId);
            if (k != known_.end() && k->second.gid == o.gid) seen.insert(o.netId);
            continue;
        }
        seen.insert(o.netId);
        auto it = known_.find(o.netId);
        if (it != known_.end() && it->second.gid != o.gid) {   // the id now names another object
            dead.push_back(rf::dLine(t, o.netId));
            known_.erase(it);
            it = known_.end();
        }
        if (it == known_.end()) {
            const bool armorLine = o.hasArmor && std::isfinite(o.maxhp) && std::isfinite(o.crit);
            born.push_back(armorLine ? rf::oLineArmor(t, o.netId, o.gid, o.tmpl, o.tid, o.team, o.maxhp, o.crit)
                                     : rf::oLine(t, o.netId, o.gid, o.tmpl, o.tid, o.team));
            Known& k = known_[o.netId];
            k = Known{ o.gid, p };
            rows.push_back(p);
            noteArmor(o, k);
            continue;
        }
        if (moved(it->second.pose, p) || turned(it->second.pose, p)) {
            it->second.pose = p;
            rows.push_back(p);
        }
        noteArmor(o, it->second);
    }
    for (auto it = known_.begin(); it != known_.end();) {
        if (seen.count(it->first)) { ++it; continue; }
        dead.push_back(rf::dLine(t, it->first));
        it = known_.erase(it);
    }
    // Every d (a reused id's, and objects gone from the sample) comes first, then the new o lines,
    // then the s line.
    for (auto& l : dead) lines.push_back(l);
    for (auto& l : born) lines.push_back(l);
    if (!rows.empty()) lines.push_back(rf::sLine(t, rows));
    if (!armor.empty()) lines.push_back(rf::aLine(t, armor));
}

void PlayerTracker::sample(double t, const std::vector<LivePlayer>& players, std::vector<std::string>& lines)
{
    // The roster marks the recording player, so it waits until he is in the list: a join's database
    // brings players a few at a time and him last (Sessions B and C).
    bool haveLocal = false;
    for (const LivePlayer& p : players) haveLocal = haveLocal || p.local;
    if (!rosterDone_ && haveLocal) {
        std::vector<rf::RosterRow> roster;
        for (const LivePlayer& p : players) roster.push_back(rf::RosterRow{ p.pid, p.team, p.ai, p.name, p.local });
        lines.push_back(rf::rosterLine(t, roster));
        rosterDone_ = true;
    }
    std::vector<rf::PlayerRow> changed;
    for (const LivePlayer& p : players) {
        rf::PlayerRow row{ p.pid, p.team, p.ctrlNetId, p.rootNetId, p.seat, 0 };
        auto it = rows_.find(p.pid);
        if (it != rows_.end() && sameRow(it->second, row)) continue;
        rows_[p.pid] = row;
        changed.push_back(row);
    }
    if (!changed.empty()) lines.push_back(rf::pLine(t, changed));
}

// ---- replay-recorder-2 (see replay_track.h) ----------------------------------------------------

void ProjectileTracker::sample(double t, const std::vector<LiveProjectile>& flights, std::vector<std::string>& lines)
{
    std::set<int> seen;
    std::vector<rf::ProjRow> rows;
    for (const LiveProjectile& p : flights) {
        if (!seen.insert(p.key).second) continue;
        auto it = flying_.find(p.key);
        if (it == flying_.end()) {
            if (flying_.size() >= 1024) { seen.erase(p.key); continue; }
            it = flying_.emplace(p.key, std::string()).first;
            lines.push_back(rf::pnLine(t, p.key, p.tmpl));
        }
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
        char at[96];
        std::snprintf(at, sizeof(at), "%.2f,%.2f,%.2f", p.x, p.y, p.z);
        if (it->second == at) continue;   // held still at the written precision
        const bool origin = std::fabs(p.x) < 0.005f && std::fabs(p.y) < 0.005f && std::fabs(p.z) < 0.005f;
        if (it->second.empty() && origin) continue;   // never yet placed away from 0,0,0
        it->second = at;
        rows.push_back({ p.key, p.x, p.y, p.z });
    }
    if (!rows.empty()) lines.push_back(rf::pjLine(t, rows));
    std::vector<int> gone;
    for (const auto& f : flying_)
        if (!seen.count(f.first)) gone.push_back(f.first);
    for (int k : gone) flying_.erase(k);
    if (!gone.empty()) lines.push_back(rf::pdLine(t, gone));
}

namespace {

// A NUL-terminated string at payload+at, at most `len` bytes and never past n.
std::string payloadStr(const uint8_t* p, size_t n, size_t at, size_t len)
{
    std::string s;
    for (size_t i = at; i < at + len && i < n && p[i]; ++i) s += (char)p[i];
    return s;
}

}  // namespace

void InfoTracker::onEvent(int type, const uint8_t* payload, size_t n)
{
    if (type == 0x1b) {
        server_ = payloadStr(payload, n, 0, 32);
    } else if (type == 0x36) {
        // "BfVietnam/levels/operation_hastings/" -> "operation_hastings"; "Conquest.con" -> "conquest"
        std::string path = payloadStr(payload, n, 0, 64), mode = payloadStr(payload, n, 64, 64);
        while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
        size_t slash = path.find_last_of("/\\");
        level_ = slash == std::string::npos ? path : path.substr(slash + 1);
        for (auto& c : mode) c = (char)std::tolower((unsigned char)c);
        if (mode.size() > 4 && mode.compare(mode.size() - 4, 4, ".con") == 0) mode.resize(mode.size() - 4);
        mode_ = mode;
    }
}

void InfoTracker::sample(double t, const std::string& player, std::vector<std::string>& lines)
{
    if (level_.empty() || player.empty()) return;
    std::string line = rf::infoLine(t, server_, level_, mode_, player);
    std::string key = line.substr(line.find(",\"server\""));   // the line without its time
    if (key == written_) return;
    written_ = key;
    lines.push_back(line);
}

void FarTracker::sample(double t, const float* view, const std::vector<SoldierPos>& soldiers, std::vector<std::string>& lines)
{
    std::map<int, bool> changed;
    std::set<int> seen;
    for (const SoldierPos& s : soldiers) {
        seen.insert(s.id);
        if (!view || !std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) continue;
        const float dx = s.x - view[0], dy = s.y - view[1], dz = s.z - view[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        const bool was = far_.count(s.id) != 0;
        if (!was && d2 > kFar * kFar) { far_.insert(s.id); changed[s.id] = true; }
        else if (was && d2 < kNear * kNear) { far_.erase(s.id); changed[s.id] = false; }
    }
    std::vector<int> gone;
    for (int id : far_)
        if (!seen.count(id)) gone.push_back(id);
    for (int id : gone) { far_.erase(id); changed[id] = false; }
    if (changed.empty()) return;
    std::vector<rf::FarRow> rows;
    for (const auto& c : changed) rows.push_back({ c.first, c.second });
    lines.push_back(rf::farLine(t, rows));
}

void CamTracker::sample(double t, const LiveCam& c, std::vector<std::string>& lines)
{
    if (!c.have) return;
    const rf::Pose p = poseFromBody(0, c.rows);
    if (!rf::finitePose(p) || (c.haveFov && !std::isfinite(c.fov))) return;
    if (c.haveMode && (!haveV_ || c.mode != mode_ || c.camTmpl != tmpl_ || c.root != root_)) {
        haveV_ = true;
        mode_ = c.mode;
        tmpl_ = c.camTmpl;
        root_ = c.root;
        lines.push_back(rf::camvLine(t, mode_, tmpl_, root_));
    }
    const rf::CamRow row{ p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw, c.fov, c.haveFov };
    if (haveCam_) {
        const float dx = row.x - last_.x, dy = row.y - last_.y, dz = row.z - last_.z;
        const bool moved = dx * dx + dy * dy + dz * dz > 0.01f * 0.01f;
        const bool turned = std::fabs(row.qx - last_.qx) > 0.002f || std::fabs(row.qy - last_.qy) > 0.002f
            || std::fabs(row.qz - last_.qz) > 0.002f || std::fabs(row.qw - last_.qw) > 0.002f;
        const bool lens = row.haveFov != last_.haveFov || (row.haveFov && std::fabs(row.fov - last_.fov) > 0.001f);
        if (!moved && !turned && !lens) return;
    }
    haveCam_ = true;
    last_ = row;
    lines.push_back(rf::camLine(t, row));
}

void InputTracker::sample(double t, const LiveInput* recs, int n, std::vector<std::string>& lines)
{
    if (n <= 0 || !recs[n - 1].have) return;
    const uint32_t newest = recs[n - 1].serial;
    if (have_ && newest == serial_) return;
    const bool again = !have_ || newest < serial_;
    for (int i = again ? n - 1 : 0; i < n; i++) {
        const LiveInput& r = recs[i];
        if (!r.have || (!again && r.serial <= serial_)) continue;
        const bool same = have_ && r.buttons == buttons_ && std::equal(r.axes, r.axes + 6, axes_);
        double at = t - (double)(newest - r.serial) * GAME_TICK;
        if (have_ && at < lastT_) at = lastT_;
        have_ = true;
        if (same) continue;
        std::copy(r.axes, r.axes + 6, axes_);
        buttons_ = r.buttons;
        lastT_ = at;
        lines.push_back(rf::inLine(at, std::vector<int>(r.axes, r.axes + 6), r.buttons));
    }
    serial_ = newest;
}
