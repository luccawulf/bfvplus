// Stages 2-3 trackers: what changed since the last sample, by BF42++'s thresholds (hit points 0.05,
// parts 0.002 a quaternion component, engines 0.01, soldier aim 1 degree), and armour on the objects.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "check.h"
#include "replay_detail.h"
#include "replay_track.h"

namespace {

int count(const std::vector<std::string>& v, const char* kind)
{
    int n = 0;
    for (auto& l : v) n += l.rfind(std::string("{\"k\":\"") + kind + "\"", 0) == 0;
    return n;
}

bool anyNan(const std::vector<std::string>& v)
{
    for (auto& l : v)
        if (l.find("nan") != std::string::npos || l.find("inf") != std::string::npos) return true;
    return false;
}

LiveObject armoured(float hp, int lastHit = -1)
{
    LiveObject o{};
    o.netId = 600; o.gid = 100; o.tmpl = "M113"; o.tid = 4479; o.team = 0;
    const float rows[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 800, 50, 900, 1 };
    for (int i = 0; i < 16; ++i) o.rows[i] = rows[i];
    o.hasArmor = true; o.hp = hp; o.maxhp = 100; o.crit = 6; o.lastHit = lastHit;
    return o;
}

void yawRows(float deg, float px, float py, float pz, float r[16])
{
    const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    const float rows[16] = { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, px, py, pz, 1 };
    std::memcpy(r, rows, sizeof(rows));
}

LivePart part(uintptr_t key, int root, float yawDeg)
{
    LivePart p{};
    p.key = key; p.tmplKey = 0x1000; p.rootNetId = root; p.name = "CannonYaw";
    yawRows(0, 100, 0, 100, p.rootRows);
    yawRows(yawDeg, 101, 1, 100, p.rows);
    return p;
}

LiveEngine engine(float revs, int flags = 1)
{
    LiveEngine e{};
    e.key = 0x5000; e.tmplKey = 0x2000; e.rootNetId = 636; e.revs = revs; e.throttle = 1; e.flags = flags; e.gear = 1;
    return e;
}

LiveSoldier soldier(float pitch, int item = 3)
{
    LiveSoldier s{};
    s.netId = 925; s.f = SoldierFields{ 5, 253, pitch, 0.0f, item, 1 };
    return s;
}

}  // namespace

TEST(armor_first_and_threshold)
{
    ObjectTracker tr;
    std::vector<std::string> l;
    tr.sample(1, { armoured(100) }, l);
    CHECK_EQ(l.size(), size_t(3));
    CHECK_EQ(l[0], std::string("{\"k\":\"o\",\"t\":1.000,\"id\":600,\"gid\":100,\"tmpl\":\"M113\",\"tid\":4479,\"team\":0,\"maxhp\":100.0,\"crit\":6.0}"));
    CHECK_EQ(count(l, "s"), 1);
    CHECK_EQ(l[2], std::string("{\"k\":\"a\",\"t\":1.000,\"a\":[[600,100.0,-1]]}"));
    l.clear();
    tr.sample(2, { armoured(99.96f) }, l);
    CHECK_EQ(l.size(), size_t(0));
    tr.sample(3, { armoured(99.94f) }, l);
    CHECK_EQ(l.size(), size_t(1));
    CHECK_EQ(count(l, "a"), 1);
    l.clear();
    tr.sample(4, { armoured(99.94f, 3) }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"a\",\"t\":4.000,\"a\":[[600,99.9,3]]}" });
}

TEST(armor_not_finite_skipped)
{
    ObjectTracker tr;
    std::vector<std::string> l;
    tr.sample(1, { armoured(100) }, l);
    l.clear();
    tr.sample(2, { armoured(NAN) }, l);
    CHECK_EQ(count(l, "a"), 0);
    CHECK(!anyNan(l));
    tr.sample(3, { armoured(99.0f) }, l);
    CHECK_EQ(count(l, "a"), 1);
    // A first sight whose max hit points aren't numbers writes stage 1's o, and no a until finite.
    ObjectTracker tr2;
    LiveObject o = armoured(NAN);
    o.maxhp = INFINITY;
    l.clear();
    tr2.sample(1, { o }, l);
    CHECK_EQ(l[0], std::string("{\"k\":\"o\",\"t\":1.000,\"id\":600,\"gid\":100,\"tmpl\":\"M113\",\"tid\":4479,\"team\":0}"));
    CHECK_EQ(count(l, "a"), 0);
    CHECK(!anyNan(l));
}

TEST(no_armor_unchanged)
{
    ObjectTracker tr;
    std::vector<std::string> l;
    LiveObject o = armoured(100);
    o.hasArmor = false;
    tr.sample(1, { o }, l);
    CHECK_EQ(l.size(), size_t(2));
    CHECK_EQ(l[0], std::string("{\"k\":\"o\",\"t\":1.000,\"id\":600,\"gid\":100,\"tmpl\":\"M113\",\"tid\":4479,\"team\":0}"));
    CHECK_EQ(count(l, "s"), 1);
}

TEST(tickets_on_change)
{
    TicketTracker tr;
    std::vector<std::string> l;
    tr.sample(0, LiveTickets{ false, 0, 0 }, l);
    CHECK(l.empty());
    tr.sample(1, LiveTickets{ true, 600, 600 }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"tk\",\"t\":1.000,\"v\":[600,600]}" });
    l.clear();
    tr.sample(2, LiveTickets{ true, 600, 600 }, l);
    CHECK(l.empty());
    tr.sample(3, LiveTickets{ true, 600, 599 }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"tk\",\"t\":3.000,\"v\":[600,599]}" });
}

TEST(cp_first_then_team)
{
    CpTracker tr;
    std::vector<std::string> l;
    LiveControlPoint cp{ 545, "OI_base1", "US_base", true, 841.57f, 35.14f, 528.64f, 2 };
    tr.sample(0, { cp }, l);
    CHECK_EQ(l, std::vector<std::string>{
        "{\"k\":\"cp\",\"t\":0.000,\"id\":545,\"name\":\"OI_base1\",\"tmpl\":\"US_base\",\"pos\":[841.57,35.14,528.64],\"team\":2}" });
    l.clear();
    tr.sample(1, { cp }, l);
    CHECK(l.empty());
    cp.team = 0;
    tr.sample(2, { cp }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"cp\",\"t\":2.000,\"id\":545,\"team\":0}" });
}

TEST(parts_jn_then_j)
{
    PartTracker tr;
    std::vector<std::string> l;
    tr.sample(0, { part(0xA000, 652, 0) }, {}, l);
    CHECK_EQ(l.size(), size_t(2));
    CHECK_EQ(l[0], std::string("{\"k\":\"jn\",\"t\":0.000,\"o\":[[652,1,\"CannonYaw\",1.00,1.00,0.00]]}"));
    CHECK_EQ(l[1], std::string("{\"k\":\"j\",\"t\":0.000,\"o\":[[652,1,0.0000,0.0000,0.0000,1.0000]]}"));
    l.clear();
    tr.sample(1, { part(0xA000, 652, 0.1f) }, {}, l);   // qy 0.00087
    CHECK(l.empty());
    tr.sample(2, { part(0xA000, 652, 0.4f) }, {}, l);   // qy 0.0035
    CHECK_EQ(count(l, "j"), 1);
    CHECK_EQ(count(l, "jn"), 0);
}

TEST(part_rekeyed_on_new_root)
{
    PartTracker tr;
    std::vector<std::string> l;
    tr.sample(0, { part(0xA000, 652, 0) }, {}, l);
    l.clear();
    tr.sample(1, { part(0xA000, 700, 0) }, {}, l);
    CHECK_EQ(count(l, "jn"), 1);
    CHECK(l[0].find("[700,2,\"CannonYaw\"") != std::string::npos);
}

TEST(engine_rows)
{
    PartTracker tr;
    std::vector<std::string> l;
    tr.sample(0, { part(0xA000, 652, 0) }, { engine(0.248f) }, l);
    CHECK_EQ(l.back(), std::string("{\"k\":\"g\",\"t\":0.000,\"o\":[[636,0.248,1.000,1,1,2]]}"));
    l.clear();
    tr.sample(1, { part(0xA000, 652, 0) }, { engine(0.253f) }, l);
    CHECK(l.empty());
    tr.sample(2, { part(0xA000, 652, 0) }, { engine(0.27f) }, l);
    CHECK_EQ(count(l, "g"), 1);
    l.clear();
    tr.sample(3, { part(0xA000, 652, 0) }, { engine(0.27f, 3) }, l);
    CHECK_EQ(count(l, "g"), 1);
}

TEST(soldier_rows)
{
    SoldierTracker tr;
    std::vector<std::string> l;
    AnimTable table{ { { 5, "Lb_Stand", 0 } }, 1 };
    tr.sample(0, { soldier(0) }, &table, l);
    CHECK_EQ(l.back(), std::string("{\"k\":\"st\",\"t\":0.000,\"o\":[[925,5,253,0.0,0.0,3,1]]}"));
    l.clear();
    tr.sample(1, { soldier(0.5f) }, nullptr, l);
    CHECK(l.empty());
    tr.sample(2, { soldier(1.5f) }, nullptr, l);
    CHECK_EQ(count(l, "st"), 1);
    l.clear();
    tr.sample(3, { soldier(1.5f, 4) }, nullptr, l);
    CHECK_EQ(count(l, "st"), 1);
}

TEST(anim_table_once)
{
    SoldierTracker tr;
    std::vector<std::string> l;
    CHECK(tr.needsAnimTable());
    AnimTable table{ { { 3323, "BigExplosion", 0 } }, 1 };
    tr.sample(0, { soldier(0) }, &table, l);
    CHECK_EQ(l.size(), size_t(2));
    CHECK_EQ(l[0], std::string("{\"k\":\"anim\",\"t\":0.000,\"states\":[[3323,\"BigExplosion\",0]]}"));
    CHECK(!tr.needsAnimTable());
    SoldierTracker tr2;
    l.clear();
    tr2.sample(0, { soldier(0) }, nullptr, l);
    CHECK_EQ(count(l, "anim"), 0);
    CHECK(!tr2.needsAnimTable());
    tr2.reset();
    CHECK(tr2.needsAnimTable());
}

TEST(anim_table_skipped)
{
    SoldierTracker tr;
    std::vector<std::string> l;
    AnimTable table{ {}, 300000 };
    tr.sample(1, { soldier(0) }, &table, l);
    CHECK_EQ(l[0], std::string("{\"k\":\"anim\",\"t\":1.000,\"skipped\":300000,\"limit\":262144,\"states\":[]}"));
}

TEST(hit_mark_rule)
{
    HitMarkTracker tr;
    std::vector<std::string> l;
    tr.sample(9.9, 3, true, 0.0f, l);
    CHECK(l.empty());
    tr.sample(10.0, 3, true, 1.0f, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"hm\",\"t\":10.000,\"pid\":3,\"at\":10.000}" });
    l.clear();
    tr.sample(10.033, 3, true, 0.97f, l);
    CHECK(l.empty());
    tr.sample(10.5, 3, true, 1.0f, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"hm\",\"t\":10.500,\"pid\":3,\"at\":10.500}" });
    l.clear();
    tr.sample(11, 3, false, 1.0f, l);
    CHECK(l.empty());
}

TEST(music_events)
{
    // Session D's radio as the server wrote it (`song` events on 679): play song 0 at 1.0 (135.2 s left),
    // a pick of song 1 at 12.7 (set, with the old song's 123.5 s left, then play at its length 203.6), stop
    // at 19.92 (196.4 s left), play again at 21.67. The client's own countdown (the 4th sample's 120.0, say)
    // runs fast, so the times left come from the tracker's clock.
    MusicTracker tr;
    std::vector<std::string> l;
    tr.sample(1, { LiveMusic{ 0x9000, 679, true, 0, 0, 135.2f } }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"e\",\"t\":1.000,\"e\":\"song\",\"a\":\"play\",\"id\":679,\"song\":0,\"group\":0,\"left\":135.2}" });
    l.clear();
    tr.sample(2, { LiveMusic{ 0x9000, 679, true, 0, 0, 120.0f } }, l);
    CHECK(l.empty());
    tr.sample(12.7, { LiveMusic{ 0x9000, 679, true, 1, 0, 203.6f } }, l);
    CHECK_EQ(l, (std::vector<std::string>{
        "{\"k\":\"e\",\"t\":12.700,\"e\":\"song\",\"a\":\"set\",\"id\":679,\"song\":1,\"group\":0,\"left\":123.5}",
        "{\"k\":\"e\",\"t\":12.700,\"e\":\"song\",\"a\":\"play\",\"id\":679,\"song\":1,\"group\":0,\"left\":203.6}" }));
    l.clear();
    tr.sample(19.9, { LiveMusic{ 0x9000, 679, false, 1, 0, 203.6f } }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"e\",\"t\":19.900,\"e\":\"song\",\"a\":\"stop\",\"id\":679,\"song\":1,\"group\":0,\"left\":196.4}" });
    l.clear();
    tr.sample(21.0, { LiveMusic{ 0x9000, 679, false, 1, 0, 203.6f } }, l);
    tr.sample(21.0, { LiveMusic{ 0x9000, 679, false, 1, 0, 203.6f }, LiveMusic{ 0x9100, 680, false, 0, 0, 0.0f } }, l);
    CHECK(l.empty());
    tr.sample(21.7, { LiveMusic{ 0x9000, 679, true, 1, 0, 203.6f } }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"e\",\"t\":21.700,\"e\":\"song\",\"a\":\"play\",\"id\":679,\"song\":1,\"group\":0,\"left\":203.6}" });
}

TEST(music_restart_and_stopped_pick)
{
    // The same song picked again while it plays starts it over (the client's time left jumps up): play.
    // A pick while stopped is a set only, with the time left where it stopped.
    MusicTracker tr;
    std::vector<std::string> l;
    tr.sample(1, { LiveMusic{ 0x9000, 679, true, 0, 0, 100.0f } }, l);
    l.clear();
    tr.sample(5, { LiveMusic{ 0x9000, 679, true, 0, 0, 90.0f } }, l);
    CHECK(l.empty());
    tr.sample(6, { LiveMusic{ 0x9000, 679, true, 0, 0, 100.0f } }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"e\",\"t\":6.000,\"e\":\"song\",\"a\":\"play\",\"id\":679,\"song\":0,\"group\":0,\"left\":100.0}" });
    l.clear();
    tr.sample(10, { LiveMusic{ 0x9000, 679, false, 0, 0, 100.0f } }, l);
    CHECK_EQ(count(l, "e"), 1);
    CHECK(l[0].find("\"left\":96.0") != std::string::npos);
    l.clear();
    tr.sample(12, { LiveMusic{ 0x9000, 679, false, 3, 0, 80.0f } }, l);
    CHECK_EQ(l, std::vector<std::string>{ "{\"k\":\"e\",\"t\":12.000,\"e\":\"song\",\"a\":\"set\",\"id\":679,\"song\":3,\"group\":0,\"left\":96.0}" });
}

TEST(shot_no_player)
{
    LiveShot s{};
    s.rootNetId = 652; s.pid = -1; s.weapon = "M60";
    yawRows(0, 10, 20, 30, s.m);
    std::string out;
    CHECK(shotLine(1.5, s, out));
    CHECK_EQ(out, std::string("{\"k\":\"f\",\"t\":1.500,\"id\":652,\"pid\":-1,\"w\":\"M60\",\"p\":[10.00,20.00,30.00],\"d\":[0.000,0.000,1.000]}"));
}

TEST(shot_bad_matrix_skipped)
{
    LiveShot s{};
    s.rootNetId = 652; s.pid = 3; s.weapon = "M60";
    yawRows(0, 10, 20, 30, s.m);
    s.m[13] = NAN;
    std::string out;
    CHECK(!shotLine(1.5, s, out));
    CHECK(out.empty());
}

TEST(shot_local)
{
    LiveShot s{};
    s.rootNetId = 652; s.pid = 0; s.weapon = "M16"; s.local = true;
    yawRows(0, 10, 20, 30, s.m);
    std::string out;
    CHECK(shotLine(2, s, out));
    CHECK(out.find(",\"local\":1}") != std::string::npos);
}

TEST(shot_queue_order_and_cap)
{
    // fireBarrel's rounds wait in the queue until the next hook writes them, oldest first; a full queue
    // (nothing draining, say in the menu) drops the newest and counts them.
    ShotQueue q(2);
    LiveShot s{};
    s.rootNetId = 1; q.push(100, s);
    s.rootNetId = 2; q.push(110, s);
    s.rootNetId = 3; q.push(120, s);
    std::vector<QueuedShot> out;
    q.drain(out);
    CHECK_EQ((int)out.size(), 2);
    CHECK(out[0].ms == 100 && out[0].shot.rootNetId == 1);
    CHECK(out[1].ms == 110 && out[1].shot.rootNetId == 2);
    CHECK_EQ((int)q.dropped(), 1);
    q.drain(out);
    CHECK(out.empty());
    q.clear();
    CHECK_EQ((int)q.dropped(), 0);
}
