/*
 * Host-compiled regression tests for the pure leader-key state machine
 * (src/leader.c / src/include/leader.h).
 *
 * No external test framework: a tiny CHECK() macro counts and reports.
 * Build & run with: make   (see tests/Makefile)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "leader.h"

/* ----- minimal assertion framework ------------------------------------- */

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg));         \
        }                                                                     \
    } while (0)

/* ----- helpers --------------------------------------------------------- */

/* A report holding a single key (keycode slot 0), no modifier. */
static leader_report_t rpt_key(uint8_t key) {
    leader_report_t r;
    memset(&r, 0, sizeof r);
    r.keycode[0] = key;
    return r;
}

/* An empty report: nothing held. */
static leader_report_t rpt_empty(void) {
    leader_report_t r;
    memset(&r, 0, sizeof r);
    return r;
}

/* A report holding two keys (no modifier). */
static leader_report_t rpt_key2(uint8_t k0, uint8_t k1) {
    leader_report_t r;
    memset(&r, 0, sizeof r);
    r.keycode[0] = k0;
    r.keycode[1] = k1;
    return r;
}

/* Standard config used across tests (double-tap mode). */
static leader_cfg_t cfg_double(void) {
    leader_cfg_t c = {
        .leader_key    = 0x47, /* Scroll Lock-ish */
        .select_a      = 0x3A,
        .select_b      = 0x3B,
        .toggle        = 0x3C,
        .double_tap    = true,
        .tap_window_ms = 400,
        .timeout_ms    = 1500,
    };
    return c;
}

/* Microsecond helper. */
#define MS(x) ((uint64_t)(x) * 1000ull)

/* Fresh IDLE state. */
static leader_state_t fresh_state(void) {
    leader_state_t s;
    memset(&s, 0, sizeof s);
    s.state = LEADER_IDLE;
    return s;
}

/* ----- test cases ------------------------------------------------------ */

/* 1. Normal typing passes through and leaves the machine in IDLE. */
static void test_normal_typing_passes(void) {
    printf("test_normal_typing_passes\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();

    const uint8_t keys[] = {0x04, 0x16, 0x07, 0x09, 0x28, 0x2C}; /* a,s,d,f,enter,space */
    uint64_t t = MS(1);
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        leader_report_t down = rpt_key(keys[i]);
        leader_report_t up = rpt_empty();
        CHECK(leader_process(&s, &cfg, &down, t) == LEADER_PASS, "typing key passes");
        CHECK(s.state == LEADER_IDLE, "stays IDLE on key down");
        t += MS(10);
        CHECK(leader_process(&s, &cfg, &up, t) == LEADER_PASS, "typing release passes");
        CHECK(s.state == LEADER_IDLE, "stays IDLE on release");
        t += MS(10);
    }
}

/* Drive a full double-tap and then a selector; assert every intermediate
 * report is CONSUMEd and the selector returns `expect`. */
static void run_double_tap_select(leader_result_t expect, uint8_t selector,
                                   const char *label) {
    printf("%s\n", label);
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();

    /* first tap down */
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "1st tap down consumed");
    t += MS(20);
    /* first tap up */
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "1st tap up consumed");
    t += MS(20);
    /* second tap down (within window) -> armed */
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "2nd tap down consumed");
    t += MS(20);
    /* second tap up (still armed) */
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "2nd tap up consumed");
    t += MS(20);
    /* selector within timeout -> switch */
    leader_report_t sel = rpt_key(selector);
    CHECK(leader_process(&s, &cfg, &sel, t) == expect, "selector fires switch");
    CHECK(s.state == LEADER_SWALLOW, "state SWALLOW after switch");

    /* nothing was forwarded before the switch (all CONSUME/SWITCH, no PASS) */
}

/* 2. Double-tap then selector A / B / toggle. */
static void test_double_tap_selectors(void) {
    leader_cfg_t cfg = cfg_double();
    run_double_tap_select(LEADER_SWITCH_A, cfg.select_a, "test_double_tap_select_a");
    run_double_tap_select(LEADER_SWITCH_B, cfg.select_b, "test_double_tap_select_b");
    run_double_tap_select(LEADER_SWITCH_TOGGLE, cfg.toggle, "test_double_tap_toggle");
}

/* 2b. Selector held (not released) before the second tap-up also works:
 * leader down -> up -> leader down -> selector while leader still conceptually
 * released. Covers the "or held" wording by firing selector immediately after
 * the second tap without an intervening empty report. */
static void test_double_tap_selector_immediately(void) {
    printf("test_double_tap_selector_immediately\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();
    leader_report_t sel = rpt_key(cfg.select_a);

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "1st down consumed"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "1st up consumed");   t += MS(20);
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "2nd down consumed"); t += MS(20);
    /* go straight to selector (leader released as selector pressed) */
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_SWITCH_A, "selector A fires");
    CHECK(s.state == LEADER_SWALLOW, "SWALLOW after immediate selector");
}

/* 3. Single-tap mode: one leader tap arms, then selector switches. */
static void test_single_tap_mode(void) {
    printf("test_single_tap_mode\n");
    leader_cfg_t cfg = cfg_double();
    cfg.double_tap = false;
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();
    leader_report_t sel = rpt_key(cfg.toggle);

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "single tap down consumed");
    CHECK(s.state == LEADER_ARMED, "armed after single tap");
    t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "single tap up consumed");
    t += MS(20);
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_SWITCH_TOGGLE, "selector toggles");
    CHECK(s.state == LEADER_SWALLOW, "SWALLOW after single-tap switch");
}

/* 4. Divergence: after arming, a NON-selector key -> PASS and back to IDLE. */
static void test_divergence_passes(void) {
    printf("test_divergence_passes\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "1st down"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "1st up");   t += MS(20);
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "2nd down -> armed"); t += MS(20);
    CHECK(s.state == LEADER_ARMED, "armed");
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "armed, released"); t += MS(20);

    leader_report_t other = rpt_key(0x04); /* 'a', not a selector */
    CHECK(leader_process(&s, &cfg, &other, t) == LEADER_PASS, "non-selector passes");
    CHECK(s.state == LEADER_IDLE, "abandoned -> IDLE");
}

/* 5. Double-tap window expiry: second tap too late -> no arm / no switch. */
static void test_double_tap_window_expiry(void) {
    printf("test_double_tap_window_expiry\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "1st down"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "1st up");

    /* second tap arrives after the tap window (400ms) */
    t += MS(500);
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "late 2nd tap -> fresh first tap");
    CHECK(s.state == LEADER_FIRST_TAP, "treated as a fresh first tap, not ARMED");
    t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "release"); t += MS(20);

    /* a selector now must NOT switch (we never got to ARMED) */
    leader_report_t sel = rpt_key(cfg.select_a);
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_PASS, "selector not a switch");
    CHECK(s.state == LEADER_IDLE, "back to IDLE, no switch");
}

/* 6. Selector-window expiry: selector after timeout -> PASS, not switch. */
static void test_selector_window_expiry(void) {
    printf("test_selector_window_expiry\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "1st down"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "1st up");   t += MS(20);
    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "2nd down -> armed"); t += MS(20);
    CHECK(s.state == LEADER_ARMED, "armed");
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "released, still armed");

    /* selector arrives after the selector timeout (1500ms) */
    t += MS(2000);
    leader_report_t sel = rpt_key(cfg.select_b);
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_PASS, "late selector forwarded");
    CHECK(s.state == LEADER_IDLE, "timed out -> IDLE");
}

/* 7. Swallow-until-release: after a switch, held keys CONSUME until empty. */
static void test_swallow_until_release(void) {
    printf("test_swallow_until_release\n");
    leader_cfg_t cfg = cfg_double();
    cfg.double_tap = false; /* quicker path to a switch */
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t ld = rpt_key(cfg.leader_key);
    leader_report_t up = rpt_empty();
    leader_report_t sel = rpt_key(cfg.select_a);

    CHECK(leader_process(&s, &cfg, &ld, t) == LEADER_CONSUME, "arm"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_CONSUME, "armed released"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_SWITCH_A, "switch A"); t += MS(20);
    CHECK(s.state == LEADER_SWALLOW, "SWALLOW");

    /* selector still physically held -> keep consuming */
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_CONSUME, "held selector swallowed"); t += MS(20);
    CHECK(leader_process(&s, &cfg, &sel, t) == LEADER_CONSUME, "still swallowed"); t += MS(20);

    /* full release -> PASS and reset */
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_PASS, "release ends swallow");
    CHECK(s.state == LEADER_IDLE, "reset to IDLE after release");
}

/* 8. Leader pressed together with another key does NOT arm. */
static void test_leader_with_other_key_no_arm(void) {
    printf("test_leader_with_other_key_no_arm\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    /* leader + 'a' in the same report */
    leader_report_t combo = rpt_key2(cfg.leader_key, 0x04);
    CHECK(leader_process(&s, &cfg, &combo, t) == LEADER_PASS, "leader+other passes");
    CHECK(s.state == LEADER_IDLE, "no arm with other key held");

    /* leader + modifier also must not arm */
    leader_report_t withmod = rpt_key(cfg.leader_key);
    withmod.modifier = 0x02; /* left shift */
    s = fresh_state();
    CHECK(leader_process(&s, &cfg, &withmod, t) == LEADER_PASS, "leader+modifier passes");
    CHECK(s.state == LEADER_IDLE, "no arm with modifier held");
}

/* 9. A selector pressed while NOT armed behaves as a normal key (PASS). */
static void test_selector_when_idle_passes(void) {
    printf("test_selector_when_idle_passes\n");
    leader_cfg_t cfg = cfg_double();
    leader_state_t s = fresh_state();
    uint64_t t = MS(100);

    leader_report_t a = rpt_key(cfg.select_a);
    leader_report_t b = rpt_key(cfg.select_b);
    leader_report_t tg = rpt_key(cfg.toggle);
    leader_report_t up = rpt_empty();

    CHECK(leader_process(&s, &cfg, &a, t) == LEADER_PASS, "select_a passes when idle");
    CHECK(s.state == LEADER_IDLE, "idle after select_a");
    t += MS(10);
    CHECK(leader_process(&s, &cfg, &up, t) == LEADER_PASS, "release passes");
    t += MS(10);
    CHECK(leader_process(&s, &cfg, &b, t) == LEADER_PASS, "select_b passes when idle");
    t += MS(10);
    CHECK(leader_process(&s, &cfg, &tg, t) == LEADER_PASS, "toggle passes when idle");
    CHECK(s.state == LEADER_IDLE, "idle after selectors");
}

/* ----- main ------------------------------------------------------------ */

int main(void) {
    test_normal_typing_passes();
    test_double_tap_selectors();
    test_double_tap_selector_immediately();
    test_single_tap_mode();
    test_divergence_passes();
    test_double_tap_window_expiry();
    test_selector_window_expiry();
    test_swallow_until_release();
    test_leader_with_other_key_no_arm();
    test_selector_when_idle_passes();

    printf("\nleader: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        printf("leader: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}
