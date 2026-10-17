#include <cstring>
#include "Camera.h"
#include "EspConfig.h"
#include "FriendList.h"
#include "Presets.h"
#include "ProfileManager.h"
#include "Smoother.h"
#include "Wire.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b, double e = 1e-3) { return std::fabs(a - b) <= e; }

int main() {
    // --- JSON from the ORIGINAL C# tool (PascalCase, 2.0 floats, extra unknown key) loads
    std::string csharp = R"({"HotkeyToggle":"F6","HotkeyConfig":"Insert","HotkeyExit":"End","DistanceFormat":"Kilometers",
      "MaxLabelDist":50,"TextShadow":false,"BoxStyle":"Corner-only","CornerRadius":6.0,"BoxThickness":3,"BoxColorMode":"Gradient",
      "NameSize":17,"BoxFillOpacity":0.2,"TracerOrigin":"Center","TracerColor":"White","TracerOpacity":0.5,"TracerTarget":"Head",
      "MaxPlayerDist":100,"MinimapSize":220,"MinimapRange":50,"OverlayOpacity":0.85,"FpsCap":120,"SomethingNew":{"a":[1,2]}})";
    auto o = json::ParseObject(csharp);
    CHECK(o.has_value());
    Settings s; s.Apply(EspConfig::FromJson(*o));
    CHECK(s.vkToggle == 0x75); CHECK(s.distFmtIdx == 1); CHECK(s.labelDistIdx == 1); CHECK(!s.textShadow);
    CHECK(s.boxStyleIdx == 2); CHECK(s.cornerRadiusIdx == 2); CHECK(s.boxThickIdx == 2); CHECK(s.playerColorIdx == 1);
    CHECK(s.nameSizeIdx == 2); CHECK(s.boxFillIdx == 2); CHECK(s.tracerOriginIdx == 1); CHECK(s.tracerColorIdx == 1);
    CHECK(s.tracerOpacityIdx == 1); CHECK(s.tracerTargetIdx == 2); CHECK(s.maxPlayerDistIdx == 1);
    CHECK(s.minimapSizeIdx == 2); CHECK(s.minimapRangeIdx == 0); CHECK(s.overlayOpacityIdx == 1); CHECK(s.fpsCap == 1);
    CHECK(s.fovIdx == 1 && s.vsync);                       // keys absent in the C# file -> defaults

    // --- Build -> JSON -> parse -> Apply is lossless
    EspConfig built = s.Build();
    auto o2 = json::ParseObject(built.ToJson());
    CHECK(o2.has_value());
    Settings s2; s2.Apply(EspConfig::FromJson(*o2));
    CHECK(s2.Build().ToJson() == built.ToJson());
    CHECK(built.MaxLabelDist == 50 && built.BoxStyle == "Corner-only" && built.HotkeyToggle == "F6");
    Settings d; CHECK(d.Build().MaxLabelDist == -1 && d.Build().MaxPlayerDist == -1 && d.Build().FpsCap == 0);

    // --- Unlimited sentinel + malformed JSON
    std::string err; CHECK(!json::ParseObject("{\"a\": }", &err).has_value()); CHECK(!err.empty());
    CHECK(!json::ParseObject("not json").has_value());

    // --- Settings save/load on disk
    s.Save(); Settings s3; s3.Load(); CHECK(s3.Build().ToJson() == s.Build().ToJson());

    // --- Friends: persistence, case-insensitive, blanks
    FriendList::Load();
    CHECK(FriendList::Add("Bob")); CHECK(!FriendList::Add("bob")); CHECK(!FriendList::Add("   ")); CHECK(FriendList::Add("alice"));
    CHECK(FriendList::Contains("BOB")); CHECK(FriendList::Names().size() == 2);
    FriendList::Remove("alice"); CHECK(!FriendList::Contains("alice"));
    { std::ifstream f("friends.json"); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("Bob") != std::string::npos); }

    // --- Profiles: sanitise, save, load, list, delete
    ProfileManager::Refresh();
    CHECK(ProfileManager::Save("My Profile!<>", s.Build()));         // -> "My Profile"
    CHECK(!ProfileManager::Save("<<>>", s.Build()));
    CHECK(ProfileManager::ProfileNames().size() == 1 && ProfileManager::ProfileNames()[0] == "My Profile");
    auto lp = ProfileManager::Load("My Profile"); CHECK(lp && lp->BoxStyle == "Corner-only");
    CHECK(!ProfileManager::Load("nope")); CHECK(ProfileManager::Delete("My Profile")); CHECK(ProfileManager::ProfileNames().empty());

    // --- Presets
    CHECK(presets::VkToName(0x2E) == "Delete" && presets::NameToVk("insert") == 0x2D && presets::NameToVk("q") == 'Q' && presets::NameToVk("0x75") == 0x75);
    CHECK(presets::IsJunkItem("rotten flesh") && !presets::IsJunkItem("Diamond"));

    // --- Wire: a REAL datagram from the agent (v2), v1 and garbage
    WorldSnapshot w;
    const char* v2 = "MCRADAR 2\nS agent ok: 2 other player(s)\nL Alice 97.213 64.000 -45.849 -9.24 20.00 17.50\nP Bob 110.000 64.000 -41.672 45.00 10.00 -\nP Carol 90 70 -60 - - -\n";
    CHECK(ParseDatagram(v2, std::strlen(v2), w));
    CHECK(w.localValid && w.local.name == "Alice" && near(w.local.pos.z, -45.849) && w.local.healthValid && near(w.local.health, 17.5) && near(w.local.pitchDeg, 20));
    CHECK(w.players.size() == 2 && !w.players[0].healthValid && w.players[0].pitchValid && !w.players[1].yawValid);
    const char* v1 = "MCRADAR 1\nS ok\nL A 1 2 3 45.5\nP B 4 5 6 -\n";
    CHECK(ParseDatagram(v1, std::strlen(v1), w) && near(w.local.yawDeg, 45.5) && !w.local.pitchValid && w.players.size() == 1);
    CHECK(!ParseDatagram("garbage\n", 8, w));

    // --- Camera: yaw 0 faces +Z; player's right is -X
    Camera c; c.eye = {0, 1.62, 0}; c.yawDeg = 0; c.pitchDeg = 0; c.fovYDeg = 70; c.w = 1920; c.h = 1080; c.Update();
    float x, y;
    CHECK(c.Project({0, 1.62, 10}, x, y) && near(x, 960, 0.5) && near(y, 540, 0.5));      // dead ahead -> screen centre
    CHECK(c.Project({-3, 1.62, 10}, x, y) && x > 960);                                    // -X is to the RIGHT at yaw 0
    CHECK(c.Project({3, 1.62, 10}, x, y) && x < 960);
    CHECK(c.Project({0, 5.62, 10}, x, y) && y < 540);                                     // higher -> up on screen
    CHECK(!c.Project({0, 1.62, -10}, x, y));                                              // behind
    // known geometry: a point 10 m ahead, tan(35)=0.7002 -> vertical half-height 7.002 m maps to the top edge
    CHECK(c.Project({0, 1.62 + 10 * std::tan(35 * 3.14159265358979 / 180), 10}, x, y) && near(y, 0, 0.5));
    // yaw 90 faces -X : a target at -X is dead ahead
    c.yawDeg = 90; c.Update(); CHECK(c.Project({-10, 1.62, 0}, x, y) && near(x, 960, 0.5));
    // yaw -90 faces +X ; yaw 180 faces -Z
    c.yawDeg = -90; c.Update(); CHECK(c.Project({10, 1.62, 0}, x, y) && near(x, 960, 0.5));
    c.yawDeg = 180; c.Update(); CHECK(c.Project({0, 1.62, -10}, x, y) && near(x, 960, 0.5));
    // pitch +45 looks DOWN: a target below-ahead is centred
    c.yawDeg = 0; c.pitchDeg = 45; c.Update(); CHECK(c.Project({0, 1.62 - 10, 10}, x, y) && near(y, 540, 0.5));
    // up vector sanity (unit, perpendicular)
    CHECK(near(Camera::Dot(c.u, c.f), 0) && near(Camera::Dot(c.u, c.r), 0) && near(Camera::Dot(c.u, c.u), 1));

    // --- Smoother: constant velocity 10 m/s sampled at 20 Hz -> 50 ms ahead = +0.5 m ; yaw wrap 179 -> -179 continues past 180
    Smoother sm; WorldSnapshot a, b; auto t0 = Smoother::Clock::now();
    PlayerState p; p.name = "Bob"; p.yawValid = true; p.pos = {0, 0, 0}; p.yawDeg = 179;
    a.players = {p}; a.localValid = true; a.local = p; a.arrived = t0; a.seq = 1;
    p.pos.x = 0.5; p.yawDeg = -179; b.players = {p}; b.localValid = true; b.local = p; b.arrived = t0 + std::chrono::milliseconds(50); b.seq = 2;
    sm.Push(a); sm.Push(b);
    auto pr = sm.Predict(b.arrived + std::chrono::milliseconds(50), true);
    CHECK(near(pr.players[0].pos.x, 1.0, 1e-3)); CHECK(near(pr.players[0].yawDeg, -177, 1e-2));
    CHECK(near(sm.Predict(b.arrived + std::chrono::milliseconds(500), true).players[0].pos.x, 1.5, 1e-3));   // capped at 100 ms of look-ahead
    CHECK(near(sm.Predict(b.arrived + std::chrono::milliseconds(50), false).players[0].pos.x, 0.5, 1e-9));   // disabled

    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails;
}
