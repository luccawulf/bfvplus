// What changed since the last sample (see replay_track.h).
#include "replay_track.h"
#include <cmath>
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
