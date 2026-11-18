/*
 * Host-compiled regression tests for the pure COM-port injection parser
 * (src/inject.c / src/include/inject.h).
 *
 * No external test framework: a tiny CHECK() macro counts and reports.
 * Build & run with: make   (see tests/Makefile)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "inject.h"

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

/* ----- HID constants mirrored from inject.c (US layout) ---------------- */

enum {
    KC_A = 0x04, KC_1 = 0x1E, KC_0 = 0x27,
    KC_ENTER = 0x28, KC_ESC = 0x29, KC_SPACE = 0x2C,
    KC_MINUS = 0x2D, KC_SEMICOLON = 0x33, KC_SLASH = 0x38,
    KC_F1 = 0x3A, KC_DELETE = 0x4C, KC_UP = 0x52, KC_F12 = 0x45,
};

enum {
    MOD_CTRL  = 0x01,
    MOD_SHIFT = 0x02,
    MOD_ALT   = 0x04,
    MOD_GUI   = 0x08,
};

/* ----- helpers --------------------------------------------------------- */

/* Convenience: run inject_ascii_to_hid and capture both outputs. */
static bool ascii(char c, uint8_t *kc, bool *shift) {
    return inject_ascii_to_hid(c, kc, shift);
}

/* ----- test cases: inject_ascii_to_hid --------------------------------- */

static void test_ascii_to_hid(void) {
    printf("test_ascii_to_hid\n");
    uint8_t kc;
    bool shift;

    /* lowercase, no shift */
    CHECK(ascii('a', &kc, &shift) && kc == 0x04 && !shift, "'a' = 0x04 no shift");
    CHECK(ascii('z', &kc, &shift) && kc == KC_A + 25 && !shift, "'z' = 0x1D no shift");

    /* uppercase: shift + same base keycode as lowercase */
    CHECK(ascii('A', &kc, &shift) && kc == 0x04 && shift, "'A' = 0x04 + shift");
    CHECK(ascii('Z', &kc, &shift) && kc == KC_A + 25 && shift, "'Z' = 0x1D + shift");

    /* digits */
    CHECK(ascii('1', &kc, &shift) && kc == 0x1E && !shift, "'1' = 0x1E");
    CHECK(ascii('9', &kc, &shift) && kc == KC_1 + 8 && !shift, "'9' = 0x26");
    CHECK(ascii('0', &kc, &shift) && kc == 0x27 && !shift, "'0' = 0x27");

    /* space */
    CHECK(ascii(' ', &kc, &shift) && kc == 0x2C && !shift, "space = 0x2C");

    /* unshifted punctuation */
    CHECK(ascii('-', &kc, &shift) && kc == KC_MINUS && !shift, "'-' unshifted");
    CHECK(ascii(';', &kc, &shift) && kc == KC_SEMICOLON && !shift, "';' unshifted");
    CHECK(ascii('/', &kc, &shift) && kc == KC_SLASH && !shift, "'/' = 0x38 unshifted");

    /* shifted symbols */
    CHECK(ascii('!', &kc, &shift) && kc == KC_1 && shift, "'!' = '1' key + shift");
    CHECK(ascii('@', &kc, &shift) && kc == KC_1 + 1 && shift, "'@' = '2' key + shift");
    CHECK(ascii('?', &kc, &shift) && kc == KC_SLASH && shift, "'?' = '/' key + shift");
    CHECK(ascii('_', &kc, &shift) && kc == KC_MINUS && shift, "'_' = '-' key + shift");

    /* unmapped characters -> false */
    CHECK(!ascii('\t', &kc, &shift), "tab unmapped -> false");
    CHECK(!ascii((char)0x7F, &kc, &shift), "DEL 0x7F unmapped -> false");
}

/* ----- test cases: inject_keyname -------------------------------------- */

static void test_keyname(void) {
    printf("test_keyname\n");
    uint8_t code;
    bool is_mod;

    /* single character names */
    CHECK(inject_keyname("a", &code, &is_mod) && code == 0x04 && !is_mod, "'a' name");
    CHECK(inject_keyname("/", &code, &is_mod) && code == KC_SLASH && !is_mod, "'/' name");

    /* modifiers set is_modifier + correct bit */
    CHECK(inject_keyname("ctrl", &code, &is_mod) && is_mod && code == MOD_CTRL, "ctrl");
    CHECK(inject_keyname("shift", &code, &is_mod) && is_mod && code == MOD_SHIFT, "shift");
    CHECK(inject_keyname("alt", &code, &is_mod) && is_mod && code == MOD_ALT, "alt");
    CHECK(inject_keyname("gui", &code, &is_mod) && is_mod && code == MOD_GUI, "gui");
    CHECK(inject_keyname("win", &code, &is_mod) && is_mod && code == MOD_GUI, "win -> gui");

    /* named keys */
    CHECK(inject_keyname("enter", &code, &is_mod) && code == KC_ENTER && !is_mod, "enter");
    CHECK(inject_keyname("esc", &code, &is_mod) && code == KC_ESC && !is_mod, "esc");
    CHECK(inject_keyname("space", &code, &is_mod) && code == KC_SPACE && !is_mod, "space");
    CHECK(inject_keyname("up", &code, &is_mod) && code == KC_UP && !is_mod, "up");
    CHECK(inject_keyname("f1", &code, &is_mod) && code == KC_F1 && !is_mod, "f1");
    CHECK(inject_keyname("f12", &code, &is_mod) && code == KC_F1 + 11 && !is_mod, "f12");

    /* case-insensitivity */
    CHECK(inject_keyname("Enter", &code, &is_mod) && code == KC_ENTER, "Enter (case)");
    CHECK(inject_keyname("CTRL", &code, &is_mod) && is_mod && code == MOD_CTRL, "CTRL (case)");

    /* f13 and unknown -> false */
    CHECK(!inject_keyname("f13", &code, &is_mod), "f13 -> false");
    CHECK(!inject_keyname("frobnicate", &code, &is_mod), "unknown -> false");
    CHECK(!inject_keyname("", &code, &is_mod), "empty -> false");
}

/* ----- test cases: inject_parse_line ----------------------------------- */

static void test_parse_none(void) {
    printf("test_parse_none\n");
    inject_cmd_t cmd;

    inject_parse_line("", &cmd);
    CHECK(cmd.kind == INJ_NONE, "blank line -> INJ_NONE");

    inject_parse_line("   ", &cmd);
    CHECK(cmd.kind == INJ_NONE, "whitespace-only -> INJ_NONE");

    inject_parse_line("# a comment", &cmd);
    CHECK(cmd.kind == INJ_NONE, "'#' comment -> INJ_NONE");

    inject_parse_line("   # indented comment", &cmd);
    CHECK(cmd.kind == INJ_NONE, "indented '#' comment -> INJ_NONE");
}

static void test_parse_type(void) {
    printf("test_parse_type\n");
    inject_cmd_t cmd;

    inject_parse_line("type Hello World", &cmd);
    CHECK(cmd.kind == INJ_TYPE, "type -> INJ_TYPE");
    CHECK(strcmp(cmd.text, "Hello World") == 0, "type preserves spaces after first");

    /* only the first separating space is consumed; subsequent spaces kept */
    inject_parse_line("type  leading-space", &cmd);
    CHECK(cmd.kind == INJ_TYPE && strcmp(cmd.text, " leading-space") == 0,
          "type keeps 2nd+ space");

    /* exactly MAX-1 chars is accepted and kept whole */
    char line[128];
    strcpy(line, "type ");
    for (int i = 0; i < INJECT_MAX_TEXT - 1; i++)
        line[5 + i] = 'x';
    line[5 + INJECT_MAX_TEXT - 1] = '\0';
    inject_parse_line(line, &cmd);
    CHECK(cmd.kind == INJ_TYPE && strlen(cmd.text) == INJECT_MAX_TEXT - 1,
          "type of exactly MAX-1 chars accepted");

    /* one char too long is REJECTED (no silent truncation) */
    strcpy(line, "type ");
    for (int i = 0; i < INJECT_MAX_TEXT; i++)
        line[5 + i] = 'x';
    line[5 + INJECT_MAX_TEXT] = '\0';
    inject_parse_line(line, &cmd);
    CHECK(cmd.kind == INJ_ERROR, "overlong type -> INJ_ERROR (not truncated)");
}

static void test_parse_key(void) {
    printf("test_parse_key\n");
    inject_cmd_t cmd;

    inject_parse_line("key a", &cmd);
    CHECK(cmd.kind == INJ_KEY && cmd.keycode == 0x04 && cmd.modifier == 0,
          "key a -> 0x04, no modifier");

    inject_parse_line("key ctrl+alt+del", &cmd);
    CHECK(cmd.kind == INJ_KEY, "chord -> INJ_KEY");
    CHECK(cmd.modifier == (MOD_CTRL | MOD_ALT), "ctrl+alt modifier = 0x05");
    CHECK(cmd.keycode == KC_DELETE, "del keycode = 0x4C");

    /* chord with only modifiers -> error */
    inject_parse_line("key ctrl+shift", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "chord without real key -> INJ_ERROR");

    /* chord with two real keys -> error */
    inject_parse_line("key ctrl+a+b", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "chord with two real keys -> INJ_ERROR");
}

static void test_parse_move(void) {
    printf("test_parse_move\n");
    inject_cmd_t cmd;

    inject_parse_line("move 10 -5", &cmd);
    CHECK(cmd.kind == INJ_MOVE && cmd.x == 10 && cmd.y == -5, "move 10 -5");

    inject_parse_line("moveto 16000 200", &cmd);
    CHECK(cmd.kind == INJ_MOVETO && cmd.x == 16000 && cmd.y == 200, "moveto 16000 200");

    inject_parse_line("move 10 abc", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "non-numeric arg -> INJ_ERROR");

    /* range checks: relative deltas fit int16; absolute is 0..32767 */
    inject_parse_line("moveto 40000 0", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "moveto beyond 32767 -> INJ_ERROR (no wrap)");

    inject_parse_line("moveto -1 0", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "moveto negative -> INJ_ERROR");

    inject_parse_line("moveto 32767 32767", &cmd);
    CHECK(cmd.kind == INJ_MOVETO && cmd.x == 32767 && cmd.y == 32767, "moveto at max ok");

    inject_parse_line("move 40000 0", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "move delta beyond int16 -> INJ_ERROR");

    inject_parse_line("move -200 200", &cmd);
    CHECK(cmd.kind == INJ_MOVE && cmd.x == -200 && cmd.y == 200, "move negative delta ok");
}

static void test_parse_click(void) {
    printf("test_parse_click\n");
    inject_cmd_t cmd;

    inject_parse_line("click", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_LEFT, "click -> left default");

    inject_parse_line("click left", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_LEFT, "click left");

    inject_parse_line("click right", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_RIGHT, "click right");

    inject_parse_line("click middle", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_MIDDLE, "click middle");

    inject_parse_line("click bogus", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "click bogus -> INJ_ERROR");
}

static void test_parse_scroll(void) {
    printf("test_parse_scroll\n");
    inject_cmd_t cmd;

    inject_parse_line("scroll -3", &cmd);
    CHECK(cmd.kind == INJ_SCROLL && cmd.y == -3, "scroll -3");
}

static void test_parse_delay(void) {
    printf("test_parse_delay\n");
    inject_cmd_t cmd;

    inject_parse_line("delay 250", &cmd);
    CHECK(cmd.kind == INJ_DELAY && cmd.ms == 250, "delay 250");

    inject_parse_line("delay -1", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "delay -1 -> INJ_ERROR");
}

static void test_parse_target(void) {
    printf("test_parse_target\n");
    inject_cmd_t cmd;

    inject_parse_line("target local", &cmd);
    CHECK(cmd.kind == INJ_TARGET && cmd.target == INJECT_TARGET_LOCAL, "target local = 0");

    inject_parse_line("target remote", &cmd);
    CHECK(cmd.kind == INJ_TARGET && cmd.target == INJECT_TARGET_REMOTE, "target remote = 1");

    inject_parse_line("target x", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "target x -> INJ_ERROR");
}

static void test_parse_misc(void) {
    printf("test_parse_misc\n");
    inject_cmd_t cmd;

    inject_parse_line("frobnicate", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "unknown verb -> INJ_ERROR");

    /* leading whitespace before a verb is tolerated */
    inject_parse_line("   key a", &cmd);
    CHECK(cmd.kind == INJ_KEY && cmd.keycode == 0x04, "leading whitespace tolerated");

    inject_parse_line("\t click left", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_LEFT,
          "leading tab tolerated");
}

/* key_down / key_up / release_all (remote-hid-aligned hold vocabulary) */
static void test_parse_hold(void) {
    printf("test_parse_hold\n");
    inject_cmd_t cmd;

    inject_parse_line("key_down ctrl", &cmd);
    CHECK(cmd.kind == INJ_KEY_DOWN && cmd.modifier == 0x01 && cmd.keycode == 0,
          "key_down ctrl -> INJ_KEY_DOWN, ctrl modifier");

    inject_parse_line("down shift+a", &cmd);
    CHECK(cmd.kind == INJ_KEY_DOWN && cmd.modifier == 0x02 && cmd.keycode == 0x04,
          "down shift+a alias -> INJ_KEY_DOWN");

    inject_parse_line("key_up ctrl", &cmd);
    CHECK(cmd.kind == INJ_KEY_UP && cmd.modifier == 0x01, "key_up ctrl -> INJ_KEY_UP");

    inject_parse_line("up a", &cmd);
    CHECK(cmd.kind == INJ_KEY_UP && cmd.keycode == 0x04, "up a alias -> INJ_KEY_UP");

    inject_parse_line("release_all", &cmd);
    CHECK(cmd.kind == INJ_RELEASE_ALL, "release_all -> INJ_RELEASE_ALL");

    inject_parse_line("release", &cmd);
    CHECK(cmd.kind == INJ_RELEASE_ALL, "release alias -> INJ_RELEASE_ALL");

    inject_parse_line("key_down bogus+", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "key_down with bad chord -> INJ_ERROR");
}

/* shortcut: one or more space-separated chords (remote-hid keyboard_shortcut) */
static void test_parse_shortcut(void) {
    printf("test_parse_shortcut\n");
    inject_cmd_t cmd;

    /* single chord */
    inject_parse_line("shortcut ctrl+alt+del", &cmd);
    CHECK(cmd.kind == INJ_SHORTCUT && cmd.nchords == 1, "shortcut one chord");
    CHECK(cmd.chords[0].modifier == (MOD_CTRL | MOD_ALT) && cmd.chords[0].keycode == KC_DELETE,
          "shortcut chord0 = ctrl+alt+del");

    /* multiple chords, VS Code style */
    inject_parse_line("shortcut ctrl+k ctrl+s", &cmd);
    CHECK(cmd.kind == INJ_SHORTCUT && cmd.nchords == 2, "shortcut two chords");
    CHECK(cmd.chords[0].modifier == MOD_CTRL && cmd.chords[0].keycode == (KC_A + ('k' - 'a')),
          "shortcut chord0 = ctrl+k");
    CHECK(cmd.chords[1].modifier == MOD_CTRL && cmd.chords[1].keycode == (KC_A + ('s' - 'a')),
          "shortcut chord1 = ctrl+s");

    /* a chord without a real key is rejected (shortcut needs a key per chord) */
    inject_parse_line("shortcut ctrl+shift", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "shortcut modifier-only chord -> INJ_ERROR");

    /* empty shortcut -> error */
    inject_parse_line("shortcut", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "shortcut with no chord -> INJ_ERROR");

    /* 'primary' is an accepted (ctrl) modifier alias */
    inject_parse_line("shortcut primary+c", &cmd);
    CHECK(cmd.kind == INJ_SHORTCUT && cmd.chords[0].modifier == MOD_CTRL &&
          cmd.chords[0].keycode == (KC_A + ('c' - 'a')), "primary+c -> ctrl+c");
}

/* click with count + held modifiers (remote-hid mouse_click options) */
static void test_parse_click_opts(void) {
    printf("test_parse_click_opts\n");
    inject_cmd_t cmd;

    inject_parse_line("click left 2", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_LEFT && cmd.count == 2,
          "click left 2 -> double click");

    inject_parse_line("click 3", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_LEFT && cmd.count == 3,
          "click 3 -> triple, default left");

    inject_parse_line("click right ctrl+shift", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_RIGHT &&
          cmd.modifier == (MOD_CTRL | MOD_SHIFT) && cmd.count == 1,
          "click right ctrl+shift -> modifiers held, count 1");

    /* order-independent: modifiers before button, count anywhere */
    inject_parse_line("click shift middle 2", &cmd);
    CHECK(cmd.kind == INJ_CLICK && cmd.buttons == INJECT_BTN_MIDDLE &&
          cmd.modifier == MOD_SHIFT && cmd.count == 2, "click shift middle 2");

    /* count out of range -> error */
    inject_parse_line("click 0", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "click 0 -> INJ_ERROR");
    inject_parse_line("click 99", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "click 99 (> max) -> INJ_ERROR");

    /* a real key (not a modifier) in the modifier slot -> error */
    inject_parse_line("click left a", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "click with non-modifier chord -> INJ_ERROR");
}

/* mouse_down / mouse_up (remote-hid button hold/release) */
static void test_parse_mouse_hold(void) {
    printf("test_parse_mouse_hold\n");
    inject_cmd_t cmd;

    inject_parse_line("mouse_down", &cmd);
    CHECK(cmd.kind == INJ_MOUSE_DOWN && cmd.buttons == INJECT_BTN_LEFT,
          "mouse_down -> left default");

    inject_parse_line("mdown right", &cmd);
    CHECK(cmd.kind == INJ_MOUSE_DOWN && cmd.buttons == INJECT_BTN_RIGHT,
          "mdown right alias");

    inject_parse_line("mouse_up middle", &cmd);
    CHECK(cmd.kind == INJ_MOUSE_UP && cmd.buttons == INJECT_BTN_MIDDLE, "mouse_up middle");

    inject_parse_line("mup", &cmd);
    CHECK(cmd.kind == INJ_MOUSE_UP && cmd.buttons == INJECT_BTN_LEFT, "mup alias default left");

    inject_parse_line("mouse_down bogus", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "mouse_down bogus -> INJ_ERROR");
}

/* drag: press + relative move + release (remote-hid mouse_drag) */
static void test_parse_drag(void) {
    printf("test_parse_drag\n");
    inject_cmd_t cmd;

    inject_parse_line("drag 40 -20", &cmd);
    CHECK(cmd.kind == INJ_DRAG && cmd.x == 40 && cmd.y == -20 &&
          cmd.buttons == INJECT_BTN_LEFT, "drag 40 -20 -> left default");

    inject_parse_line("drag 10 10 right", &cmd);
    CHECK(cmd.kind == INJ_DRAG && cmd.buttons == INJECT_BTN_RIGHT, "drag with button");

    inject_parse_line("drag 5 5 left ctrl", &cmd);
    CHECK(cmd.kind == INJ_DRAG && cmd.buttons == INJECT_BTN_LEFT && cmd.modifier == MOD_CTRL,
          "drag with button + held modifier");

    inject_parse_line("drag 10 abc", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "drag non-numeric -> INJ_ERROR");

    inject_parse_line("drag 40000 0", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "drag delta beyond int16 -> INJ_ERROR");
}

/* scroll with held modifiers (remote-hid mouse_scroll options) */
static void test_parse_scroll_opts(void) {
    printf("test_parse_scroll_opts\n");
    inject_cmd_t cmd;

    inject_parse_line("scroll 5 ctrl", &cmd);
    CHECK(cmd.kind == INJ_SCROLL && cmd.y == 5 && cmd.modifier == MOD_CTRL,
          "scroll 5 ctrl -> zoom-style, ctrl held");

    inject_parse_line("scroll -2 shift+alt", &cmd);
    CHECK(cmd.kind == INJ_SCROLL && cmd.y == -2 && cmd.modifier == (MOD_SHIFT | MOD_ALT),
          "scroll -2 shift+alt");

    /* a button token is meaningless for scroll -> error */
    inject_parse_line("scroll 3 left", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "scroll with button token -> INJ_ERROR");

    /* missing amount -> error */
    inject_parse_line("scroll", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "scroll with no amount -> INJ_ERROR");

    /* out of int8 range -> error (would otherwise wrap / invert direction) */
    inject_parse_line("scroll 200", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "scroll 200 -> INJ_ERROR");
    inject_parse_line("scroll 127", &cmd);
    CHECK(cmd.kind == INJ_SCROLL && cmd.y == 127, "scroll 127 at max ok");
    inject_parse_line("scroll -128", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "scroll -128 (beyond -127) -> INJ_ERROR");
}

/* ----- main ------------------------------------------------------------ */

int main(void) {
    test_ascii_to_hid();
    test_keyname();
    test_parse_none();
    test_parse_type();
    test_parse_key();
    test_parse_hold();
    test_parse_shortcut();
    test_parse_move();
    test_parse_click();
    test_parse_click_opts();
    test_parse_mouse_hold();
    test_parse_drag();
    test_parse_scroll();
    test_parse_scroll_opts();
    test_parse_delay();
    test_parse_target();
    test_parse_misc();

    printf("\ninject: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        printf("inject: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    return 0;
}
