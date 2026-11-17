// Stages 2-3 trackers (see replay_detail.h).
#include "replay_detail.h"
#include <cmath>
#include <set>

namespace {

bool finite4(const float q[4])
{
    return std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]);
}

const uint64_t kPartStaleSamples = 150;   // ~5 s at 30 Hz, the server's parts_prune
const double kMusicForgetS = 5.0;         // a radio missing from samples this long is a new one when back

}  // namespace

void TicketTracker::sample(double t, const LiveTickets& now, std::vector<std::string>& lines)
{
    if (!now.valid) return;
    if (have_ && now.team1 == t1_ && now.team2 == t2_) return;
    have_ = true;
    t1_ = now.team1;
    t2_ = now.team2;
    lines.push_back(rf::tkLine(t, t1_, t2_));
}

void CpTracker::sample(double t, const std::vector<LiveControlPoint>& cps, std::vector<std::string>& lines)
{
    for (const LiveControlPoint& cp : cps) {
        auto it = teams_.find(cp.id);
        if (it == teams_.end()) {
            const float pos[3] = { cp.x, cp.y, cp.z };
            const bool hasPos = cp.hasPos && std::isfinite(cp.x) && std::isfinite(cp.y) && std::isfinite(cp.z);
            lines.push_back(rf::cpFirstLine(t, cp.id, cp.name, cp.tmpl, hasPos ? pos : nullptr, cp.team));
            teams_[cp.id] = cp.team;
        } else if (it->second != cp.team) {
            it->second = cp.team;
            lines.push_back(rf::cpLine(t, cp.id, cp.team));
        }
    }
}

PartTracker::Slot& PartTracker::slotFor(uintptr_t key, uintptr_t tmplKey, int root, bool& first)
{
    auto it = slots_.find(key);
    first = it == slots_.end() || it->second.root != root || it->second.tmplKey != tmplKey;
    Slot& s = slots_[key];
    if (first) {
        s = Slot{};
        s.tmplKey = tmplKey;
        s.root = root;
        s.id = nextId_++;
    }
    s.seen = gen_;
    return s;
}

void PartTracker::sample(double t, const std::vector<LivePart>& parts, const std::vector<LiveEngine>& engines,
    std::vector<std::string>& lines)
{
    ++gen_;
    std::vector<rf::JointName> names;
    std::vector<rf::JointRow> joints;
    std::vector<rf::EngineRow> rows;
    for (const LivePart& p : parts) {
        bool first;
        Slot& s = slotFor(p.key, p.tmplKey, p.rootNetId, first);
        float q[4], at[3];
        partRelative(p.rootRows, p.rows, q, at);
        if (first) {
            const bool hasAt = std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
            names.push_back(rf::JointName{ p.rootNetId, s.id, p.name, hasAt, at[0], at[1], at[2] });
        }
        if (!finite4(q)) continue;
        if (s.hasJ && std::fabs(s.q[0] - q[0]) <= 0.002f && std::fabs(s.q[1] - q[1]) <= 0.002f
            && std::fabs(s.q[2] - q[2]) <= 0.002f && std::fabs(s.q[3] - q[3]) <= 0.002f)
            continue;
        s.hasJ = true;
        for (int k = 0; k < 4; ++k) s.q[k] = q[k];
        joints.push_back(rf::JointRow{ p.rootNetId, s.id, q[0], q[1], q[2], q[3] });
    }
    for (const LiveEngine& e : engines) {
        bool first;
        Slot& s = slotFor(e.key, e.tmplKey, e.rootNetId, first);
        if (!std::isfinite(e.revs) || !std::isfinite(e.throttle)) continue;
        if (s.hasG && std::fabs(s.revs - e.revs) < 0.01f && std::fabs(s.throttle - e.throttle) < 0.01f
            && s.flags == e.flags && s.gear == e.gear)
            continue;
        s.hasG = true;
        s.revs = e.revs; s.throttle = e.throttle; s.flags = e.flags; s.gear = e.gear;
        rows.push_back(rf::EngineRow{ e.rootNetId, e.revs, e.throttle, e.flags, e.gear, s.id });
    }
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (gen_ - it->second.seen > kPartStaleSamples) it = slots_.erase(it);
        else ++it;
    }
    if (!names.empty()) lines.push_back(rf::jnLine(t, names));
    if (!joints.empty()) lines.push_back(rf::jLine(t, joints));
    if (!rows.empty()) lines.push_back(rf::gLine(t, rows));
}

void SoldierTracker::sample(double t, const std::vector<LiveSoldier>& soldiers, const AnimTable* table,
    std::vector<std::string>& lines)
{
    std::vector<rf::SoldierRow> rows;
    std::map<int, SoldierFields> seen;
    for (const LiveSoldier& s : soldiers) {
        const SoldierFields& f = s.f;
        seen[s.netId] = f;
        auto it = last_.find(s.netId);
        if (it != last_.end()) {
            const SoldierFields& o = it->second;
            if (o.lower == f.lower && o.upper == f.upper && o.item == f.item && o.bits == f.bits
                && std::fabs(o.pitch - f.pitch) < 1.0f && std::fabs(o.twist - f.twist) < 1.0f) {
                seen[s.netId] = o;   // keep the written aim, so a slow drift still crosses 1 degree
                continue;
            }
        }
        rows.push_back(rf::SoldierRow{ s.netId, f.lower, f.upper, f.pitch, f.twist, f.item, f.bits });
    }
    // A soldier gone from the sample (dead, or seated as a child) writes a fresh st when he is back.
    last_.swap(seen);
    if (rows.empty()) return;
    if (!animTried_) {
        animTried_ = true;
        if (table) {
            if (table->total > kAnimStatesMax) lines.push_back(rf::animSkippedLine(t, table->total, kAnimStatesMax));
            else lines.push_back(rf::animLine(t, table->states));
        }
    }
    lines.push_back(rf::stLine(t, rows));
}

void HitMarkTracker::sample(double t, int pid, bool have, float v, std::vector<std::string>& lines)
{
    if (!have || !(v > 0.0f && v <= 1.0f)) return;
    const double at = t - (1.0 - v);
    const bool mark = at > lastAt_ + 0.05;
    lastAt_ = at;
    if (mark) lines.push_back(rf::hmLine(t, pid, at));
}

void MusicTracker::sample(double t, const std::vector<LiveMusic>& players, std::vector<std::string>& lines)
{
    std::unordered_map<uintptr_t, State> seen;
    for (const LiveMusic& m : players) {
        const float client = std::isfinite(m.left) && m.left >= 0.0f && m.left < 100000.0f ? m.left : 0.0f;
        auto it = last_.find(m.key);
        State s{ m.playing, m.song, t, client, client, client, t };
        if (it == last_.end()) {
            if (m.playing) lines.push_back(rf::songLine(t, "play", m.rootNetId, m.song, m.group, client));
            seen[m.key] = s;
            continue;
        }
        const State& o = it->second;
        const float before = o.leftAt(t);
        const bool picked = m.song != o.song;
        const bool restarted = !picked && m.playing && o.playing && client > o.client + 2.0f;
        if (picked) lines.push_back(rf::songLine(t, "set", m.rootNetId, m.song, m.group, before));
        if (m.playing && (picked || restarted || !o.playing)) {
            lines.push_back(rf::songLine(t, "play", m.rootNetId, m.song, m.group, client));
        } else if (m.playing) {
            s.t0 = o.t0; s.left0 = o.left0;   // still the same play: keep its clock
        } else if (o.playing) {
            lines.push_back(rf::songLine(t, "stop", m.rootNetId, m.song, m.group, before));
            s.frozen = before;
        } else {
            s.frozen = o.frozen;              // stopped all along (a pick while stopped keeps it too)
        }
        seen[m.key] = s;
    }
    for (const auto& kv : last_)
        if (!seen.count(kv.first) && t - kv.second.seen <= kMusicForgetS) seen.insert(kv);
    last_.swap(seen);
}

bool shotLine(double t, const LiveShot& s, std::string& out)
{
    float p[3], d[3];
    if (!shotFromMatrix(s.m, p, d)) return false;
    out = rf::fLine(t, s.rootNetId, s.pid, s.weapon, p, d, s.local);
    return true;
}

void ShotQueue::push(uint64_t ms, const LiveShot& s)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (q_.size() >= cap_) { ++dropped_; return; }
    q_.push_back(QueuedShot{ ms, s });
}

void ShotQueue::drain(std::vector<QueuedShot>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lock(mu_);
    out.swap(q_);
}

void ShotQueue::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    q_.clear();
    dropped_ = 0;
}

size_t ShotQueue::dropped() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_;
}
