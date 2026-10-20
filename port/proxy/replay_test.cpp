
#include "backend.h"
#include "config.h"
#include "fixed_step.h"
#include "replay_script.h"
#include "steer.h"
#include "watch.h"
#include "window_mode.h"

#include <cstdio>
#include <cstring>

namespace {

unsigned g_failures;

void check(bool ok, const char* what, int line) {
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __LINE__)

constexpr long long kBase = 1000000000;

struct ParseCase {
    const char* line;
    replay::Parse want;
    unsigned long long frame;
    const char* name;
    unsigned nargs;
    long long arg[3];
};

const ParseCase kParse[] = {
    {"frame 0 start", replay::Parse::Event, 0, "start", 0, {0, 0, 0}},
    {"  frame 120 key 27 1  ", replay::Parse::Event, 120, "key", 2, {27, 1, 0}},
    {"frame 240 mouse 16 -20 3", replay::Parse::Event, 240, "mouse", 3, {16, -20, 3}},
    {"\tframe 9 quit\r", replay::Parse::Event, 9, "quit", 0, {0, 0, 0}},
    {"# comment under a frame script", replay::Parse::Skip, 0, "", 0, {0, 0, 0}},
    {"   ", replay::Parse::Skip, 0, "", 0, {0, 0, 0}},
    {"frame", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame x key", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame 10", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"10 key 1", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame -1 key", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame 10 key 1 2 3 4", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame 10 key down", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
    {"frame 10 abcdefghijklmnopqrstuvwx 1", replay::Parse::Bad, 0, "", 0, {0, 0, 0}},
};

const char kScript[] =
    "# a three-event script\r\n"
    "frame 0 start\n"
    "frame 60 key 27 1\n"
    "\n"
    "frame 120 quit\n";

const char kBadScript[] =
    "frame 0 start\n"
    "not a frame line\n";

void test_clock() {
    CHECK(replay::qpf() == 60000000);

    CHECK(replay::clock_qpc() == 1000);
    CHECK(replay::clock_qpc() == 2000);
    long long qpc = 2000;
    for (int i = 2; i < 1000; ++i) qpc = replay::clock_qpc();
    CHECK(qpc == 1000000);
    for (int i = 1000; i < 60000; ++i) qpc = replay::clock_qpc();
    CHECK(qpc == 60000000);

    CHECK(replay::clock_ms(0) == 16);
    CHECK(replay::clock_ms(0) == 32);
    long long ms_time = 32;
    for (int i = 2; i < 60; ++i) ms_time = replay::clock_ms(0);
    CHECK(ms_time == 960);

    CHECK(replay::clock_ms(1) == 16);
    CHECK(replay::clock_ms(0) == 976);
    CHECK(replay::clock_ms(1) == 32);

    replay::clock_filetime_base() = kBase;
    CHECK(replay::clock_filetime() == kBase + 166666);
    CHECK(replay::clock_filetime() == kBase + 333332);
    long long ft = kBase + 333332;
    for (int i = 2; i < 60; ++i) ft = replay::clock_filetime();
    CHECK(ft == kBase + 9999960);
}

void test_clock_start() {
    CHECK(!replay::clock_live());
    constexpr long long kSeed = 123456789;
    replay::clock_start(kSeed);
    CHECK(replay::clock_live());
    CHECK(replay::clock_qpc() == kSeed + 1000);
    CHECK(replay::clock_qpc() == kSeed + 2000);

    struct LimiterCase {
        int fps;
        unsigned max_queries;
    };
    const LimiterCase kLimiter[] = {{60, 1000}, {30, 2000}, {120, 500}, {240, 250}};
    for (const LimiterCase& c : kLimiter) {
        replay::clock_start(kSeed);
        long long target = replay::qpf() / c.fps;
        long long accumulated = 0;
        long long previous = replay::clock_qpc();
        unsigned queries = 0;
        while (accumulated < target && queries < 1000000) {
            long long now = replay::clock_qpc();
            accumulated += now - previous;
            previous = now;
            ++queries;
        }
        CHECK(accumulated >= target);
        CHECK(accumulated - target < 1000);
        CHECK(queries <= c.max_queries);
    }
}

void test_fixed_origin() {
    CHECK(replay::kSeed == 3000000000000LL);
    CHECK(replay::kFiletimeBase == 127805472000000000LL);
    replay::clock_filetime_base() = replay::kFiletimeBase;
    replay::clock_start(replay::kSeed);
    CHECK(replay::clock_qpc() == replay::kSeed + 1000);

    CHECK(replay::clock_filetime_now() == replay::kFiletimeBase);
    CHECK(replay::clock_filetime_now() == replay::kFiletimeBase);
    replay::clock_filetime();
    CHECK(replay::clock_filetime_now() == replay::kFiletimeBase + 166666);
}

void test_parse_line() {
    for (const ParseCase& c : kParse) {
        replay::Event event = {};
        replay::Parse got = replay::parse_line(c.line, &event);
        CHECK(got == c.want);
        if (got != replay::Parse::Event) continue;
        CHECK(event.frame == c.frame);
        CHECK(std::strcmp(event.name, c.name) == 0);
        CHECK(event.nargs == c.nargs);
        for (unsigned i = 0; i < c.nargs && i < 3; ++i) CHECK(event.arg[i] == c.arg[i]);
    }
}

void test_parse_script() {
    replay::Event events[4] = {};
    unsigned count = 0;
    CHECK(replay::parse(kScript, events, 4, &count));
    CHECK(count == 3);
    CHECK(events[0].frame == 0 && std::strcmp(events[0].name, "start") == 0);
    CHECK(events[1].frame == 60 && std::strcmp(events[1].name, "key") == 0 &&
          events[1].nargs == 2 && events[1].arg[0] == 27 && events[1].arg[1] == 1);
    CHECK(events[2].frame == 120 && std::strcmp(events[2].name, "quit") == 0);

    count = 99;
    CHECK(!replay::parse(kBadScript, events, 4, &count));
    CHECK(count == 1);

    count = 99;
    CHECK(!replay::parse(kScript, events, 2, &count));
    CHECK(count == 2);
}

void test_state() {
    const replay::Event script[] = {
        {5, "activate", {0, 0, 0}, 0},
        {10, "mouse", {10, -4, 0}, 2},
        {10, "mouse", {5, 2, 0}, 2},
        {12, "button", {0, 1, 0}, 2},
        {13, "button", {0, 0, 0}, 2},
        {14, "key", {27, 1, 0}, 2},
        {15, "key", {27, 0, 0}, 2},
        {20, "button", {4, 1, 0}, 2},
        {21, "key", {300, 1, 0}, 2},
    };
    const unsigned count = (unsigned)(sizeof script / sizeof script[0]);
    replay::State state = {};
    unsigned next = 0;
    unsigned activates = 0;
    long dx = 0;
    long dy = 0;

    CHECK(replay::apply_due(script, count, &next, 4, &state, &activates) == 0);
    CHECK(replay::apply_due(script, count, &next, 9, &state, &activates) == 1);
    CHECK(activates == 1);
    CHECK(next == 1);
    CHECK(state.dx == 0 && state.dy == 0);

    CHECK(replay::apply_due(script, count, &next, 10, &state, &activates) == 2);
    CHECK(state.dx == 15 && state.dy == -2);
    replay::take_mouse(&state, &dx, &dy);
    CHECK(dx == 15 && dy == -2);
    CHECK(state.dx == 0 && state.dy == 0);
    replay::take_mouse(&state, &dx, &dy);
    CHECK(dx == 0 && dy == 0);

    CHECK(replay::apply_due(script, count, &next, 12, &state, &activates) == 1);
    CHECK(state.buttons == 1u);
    CHECK(replay::apply_due(script, count, &next, 13, &state, &activates) == 1);
    CHECK(state.buttons == 0u);
    CHECK(replay::apply_due(script, count, &next, 14, &state, &activates) == 1);
    CHECK(state.keys[27] == 0x80);
    CHECK(replay::apply_due(script, count, &next, 15, &state, &activates) == 1);
    CHECK(state.keys[27] == 0x00);

    CHECK(replay::apply_due(script, count, &next, 21, &state, &activates) == 2);
    CHECK(state.buttons == 0u);
    CHECK(state.keys[27] == 0x00);
    CHECK(activates == 1);
    CHECK(next == count);
    CHECK(replay::apply_due(script, count, &next, 999, &state, &activates) == 0);
}

void test_joy_state() {
    const replay::Event script[] = {
        {30, "joy", {18000, 1, 0}, 2},
        {31, "joy", {-1, 0x81, 0}, 2},
    };
    const unsigned count = (unsigned)(sizeof script / sizeof script[0]);
    replay::State state = {};
    unsigned next = 0;
    unsigned activates = 0;

    CHECK(state.pov == 0 && state.joy_buttons == 0);
    CHECK(replay::apply_due(script, count, &next, 29, &state, &activates) == 0);
    CHECK(replay::apply_due(script, count, &next, 30, &state, &activates) == 1);
    CHECK(state.pov == 18000 && state.joy_buttons == 1u);
    CHECK(replay::apply_due(script, count, &next, 31, &state, &activates) == 1);
    CHECK(state.pov == -1 && state.joy_buttons == 0x81u);
    CHECK(replay::apply_due(script, count, &next, 31, &state, &activates) == 0);
}

void test_offline_answers() {
    CHECK(replay::is_browser_clsid(&replay::kBrowserClsid));
    replay::Guid other = replay::kBrowserClsid;
    other.d[0] ^= 1;
    CHECK(!replay::is_browser_clsid(&other));
    CHECK(!replay::is_browser_clsid(nullptr));
    CHECK(replay::kCursorX == 0 && replay::kCursorY == 0);
}

struct WatchCase {
    const char* text;
    bool ok;
    bool indexed;
    uint32_t field;
};

const WatchCase kWatch[] = {
    {"6D15E0+EC94@6D09D4+1E74.F20", true, true, 0xF20},
    {"0x6d15e0+0xec94@0x6d09d4+0x1e74.0xf24", true, true, 0xF24},
    {"6C8910", true, false, 0},
    {"", false, false, 0},
    {"6C8910,6C8914", false, false, 0},
    {"6C8910 garbage", false, false, 0},
};

void test_watch() {
    CHECK(watch::dr7(1) == 0x000D0101u);
    CHECK(watch::dr7(2) == 0x00DD0105u);
    for (const WatchCase& c : kWatch) {
        probe::Term term = {};
        bool ok = watch::parse(c.text, &term);
        CHECK(ok == c.ok);
        if (!ok) continue;
        CHECK(term.has_index == c.indexed);
        if (c.indexed) CHECK(term.field == c.field);
    }
}

void test_posted() {
    replay::Posted posted = {};
    CHECK(!replay::post_take(&posted, 0x11223344u));
    replay::post_add(&posted, 0x11223344u);
    replay::post_add(&posted, 0x55667788u);
    CHECK(posted.count == 2);
    CHECK(!replay::post_take(&posted, 0x55667788u));
    CHECK(posted.count == 2);
    CHECK(replay::post_take(&posted, 0x11223344u));
    CHECK(posted.count == 1);
    CHECK(replay::post_take(&posted, 0x55667788u));
    CHECK(posted.count == 0);
    for (unsigned i = 0; i < 10; ++i) replay::post_add(&posted, i);
    CHECK(posted.count == 8);
    CHECK(!replay::post_take(&posted, 8u));
    CHECK(replay::post_take(&posted, 0u));
    CHECK(replay::post_take(&posted, 1u));
}

struct FaithfulCase {
    const char* text;
    int want;
};

const FaithfulCase kFaithful[] = {
    {"faithful=1\n", 1},
    {"faithful=0\n", 0},
    {"", 0},
    {"# faithful=1\n", 0},
    {"faithful = 1\n", 0},
    {"faithful=1\r\n", 1},
    {"faithful=1", 1},
    {"  faithful=1  \n", 1},
    {"x=1\nfaithful=1\n", 1},
    {"faithful=2\n", 0},
    {"faithful=yes\n", 0},
    {"faithful=1\nfaithful=0\n", 0},
    {"faithful\n", 0},
};

void test_faithful() {
    for (const FaithfulCase& c : kFaithful) CHECK(config::faithful(c.text) == c.want);

    CHECK(config::faithful(config::kDefault) == 0);
}

void test_config_keys() {
    char v[24];
    CHECK(config::value("window=borderless\n", "window=", v, sizeof v));
    CHECK(!std::strcmp(v, "borderless"));
    CHECK(config::value("window=windowed\nwindow=borderless\n", "window=", v, sizeof v));
    CHECK(!std::strcmp(v, "borderless"));
    CHECK(config::value("  scale=fit\r\n", "scale=", v, sizeof v));
    CHECK(!std::strcmp(v, "fit"));
    CHECK(!config::value("faithful=1\n", "window=", v, sizeof v));
    CHECK(config::flag("step=0\n", "step=", 1) == 0);
    CHECK(config::flag("step=1\n", "step=", 0) == 1);
    CHECK(config::flag("", "step=", 1) == 1);
    CHECK(config::flag(config::kDefault, "step=", 0) == 1);
}

void test_backend() {
    char path[512];
    CHECK(backend::translated(0));
    CHECK(!backend::translated(1));
    backend::path(0, "C:\\game", "C:\\Windows\\SysWOW64", path, sizeof path);
    CHECK(!std::strcmp(path, "C:\\game\\..\\build\\proxy\\d3d8to9.dll"));
    backend::path(1, "C:\\game", "C:\\Windows\\SysWOW64", path, sizeof path);
    CHECK(!std::strcmp(path, "C:\\Windows\\SysWOW64\\d3d8.dll"));
}

void test_window_mode() {
    CHECK(windowmode::kind("windowed") == windowmode::kWindowed);
    CHECK(windowmode::kind("borderless") == windowmode::kBorderless);
    CHECK(windowmode::kind("fullscreen-desktop") == windowmode::kDesktop);
    CHECK(windowmode::kind("nonsense") == windowmode::kWindowed);
    CHECK(windowmode::factor("1", 600, 450, 1920, 1040) == 1);
    CHECK(windowmode::factor("2", 600, 450, 1920, 1040) == 2);
    CHECK(windowmode::factor("0", 600, 450, 1920, 1040) == 1);
    CHECK(windowmode::factor("99", 600, 450, 1920, 1040) == 8);
    CHECK(windowmode::factor("fit", 600, 450, 1920, 1040) == 2);
    CHECK(windowmode::factor("fit", 600, 450, 1280, 1024) == 2);
    CHECK(windowmode::factor("fit", 600, 450, 600, 450) == 1);
    windowmode::Plan w =
        windowmode::plan(windowmode::kWindowed, 2, 600, 450, 1920, 1040, 1920, 1080);
    CHECK(w.width == 1200 && w.height == 900 && !w.popup);
    windowmode::Plan b =
        windowmode::plan(windowmode::kBorderless, 2, 600, 450, 1920, 1040, 1920, 1080);
    CHECK(b.width == 1920 && b.height == 1040 && b.popup);
    windowmode::Plan d =
        windowmode::plan(windowmode::kDesktop, 2, 600, 450, 1920, 1040, 1920, 1080);
    CHECK(d.width == 1920 && d.height == 1080 && d.popup);
    windowmode::Rect r = windowmode::letterbox(600, 450, 1200, 900);
    CHECK(r.left == 0 && r.top == 0 && r.right == 1200 && r.bottom == 900);
    r = windowmode::letterbox(600, 450, 1920, 1040);
    CHECK(r.left == 266 && r.top == 0 && r.right == 1653 && r.bottom == 1040);
    r = windowmode::letterbox(600, 450, 800, 900);
    CHECK(r.left == 0 && r.top == 150 && r.right == 800 && r.bottom == 750);
    r = windowmode::letterbox(0, 0, 800, 900);
    CHECK(r.left == 0 && r.top == 0 && r.right == 800 && r.bottom == 900);
}

struct SteerCase {
    unsigned mask;
    int dx;
    int dy;
};

const SteerCase kSteer[] = {
    {0, 0, 0},
    {steer::kLeft, -1, 0},
    {steer::kRight, 1, 0},
    {steer::kUp, 0, -1},
    {steer::kDown, 0, 1},
    {steer::kRight | steer::kUp, 1, -1},
    {steer::kLeft | steer::kDown, -1, 1},
    {steer::kLeft | steer::kRight, 0, 0},
    {steer::kUp | steer::kDown, 0, 0},
    {steer::kLeft | steer::kUp | steer::kRight | steer::kDown, 0, 0},
};

void test_steer() {
    for (const SteerCase& c : kSteer) {
        int dx = 9;
        int dy = 9;
        steer::direction(c.mask, &dx, &dy);
        CHECK(dx == c.dx && dy == c.dy);
    }
    CHECK(steer::kPxPerPresent == 5);

    long x = 300;
    long y = 225;
    steer::step(&x, &y, 1, 0, 600, 450);
    CHECK(x == 300 + steer::kPxPerPresent && y == 225);
    steer::step(&x, &y, 0, -1, 600, 450);
    CHECK(x == 305 && y == 220);

    x = 2;
    y = 448;
    steer::step(&x, &y, -1, 1, 600, 450);
    CHECK(x == 0 && y == 449);
    x = 598;
    y = 1;
    steer::step(&x, &y, 1, -1, 600, 450);
    CHECK(x == 599 && y == 0);

    x = 100;
    y = 100;
    steer::step(&x, &y, 1, 1, 0, 0);
    CHECK(x == 100 && y == 100);

    CHECK(steer::lparam(299, 391) == 0x0187012Bu);
    CHECK(steer::lparam(0, 0) == 0);
}

void test_fixed_step() {
    CHECK(fixedstep::kMaxSteps == 5);
    CHECK(fixedstep::kSampleEvery == 60);
    float step = fixedstep::kStep;
    uint32_t bits = 0;
    std::memcpy(&bits, &step, 4);
    CHECK(bits == 0x3C888889u);

    fixedstep::Clock clock = {};
    CHECK(fixedstep::take(&clock) == 0);
    fixedstep::add(&clock, fixedstep::kStepSeconds * 0.5);
    CHECK(fixedstep::take(&clock) == 0);
    fixedstep::add(&clock, fixedstep::kStepSeconds * 0.5);
    CHECK(fixedstep::take(&clock) == 1);
    CHECK(fixedstep::take(&clock) == 0);

    fixedstep::add(&clock, fixedstep::kStepSeconds * 3.5);
    CHECK(fixedstep::take(&clock) == 3);
    fixedstep::add(&clock, fixedstep::kStepSeconds * 12.0);
    CHECK(fixedstep::take(&clock) == fixedstep::kMaxSteps);
    CHECK(fixedstep::take(&clock) == 0);

    fixedstep::add(&clock, -1.0);
    CHECK(fixedstep::take(&clock) == 0);
}

}

int main() {
    test_clock();
    test_clock_start();
    test_fixed_origin();
    test_offline_answers();
    test_watch();
    test_posted();
    test_faithful();
    test_config_keys();
    test_backend();
    test_window_mode();
    test_steer();
    test_fixed_step();
    test_parse_line();
    test_parse_script();
    test_state();
    test_joy_state();
    if (g_failures) {
        std::fprintf(stderr, "%u case(s) failed\n", g_failures);
        return 1;
    }
    std::printf("ok %u parser cases\n", (unsigned)(sizeof kParse / sizeof kParse[0]));
    return 0;
}
