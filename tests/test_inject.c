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

    /* truncation to INJECT_MAX_TEXT-1 and NUL termination */
    char line[128];
    char expect[INJECT_MAX_TEXT];
    strcpy(line, "type ");
    for (int i = 0; i < 100; i++)
        line[5 + i] = 'x';
    line[5 + 100] = '\0';
    inject_parse_line(line, &cmd);
    CHECK(cmd.kind == INJ_TYPE, "long type still INJ_TYPE");
    CHECK(strlen(cmd.text) == INJECT_MAX_TEXT - 1, "type truncated to MAX-1");
    memset(expect, 'x', INJECT_MAX_TEXT - 1);
    expect[INJECT_MAX_TEXT - 1] = '\0';
    CHECK(strcmp(cmd.text, expect) == 0, "truncated content correct + NUL-terminated");
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

    inject_parse_line("shortcut ctrl+alt+del", &cmd);
    CHECK(cmd.kind == INJ_KEY && cmd.modifier == 0x05 && cmd.keycode == 0x4C,
          "shortcut alias -> INJ_KEY (tap)");

    inject_parse_line("key_down bogus+", &cmd);
    CHECK(cmd.kind == INJ_ERROR, "key_down with bad chord -> INJ_ERROR");
}

/* ----- main ------------------------------------------------------------ */

int main(void) {
    test_ascii_to_hid();
    test_keyname();
    test_parse_none();
    test_parse_type();
    test_parse_key();
    test_parse_hold();
    test_parse_move();
    test_parse_click();
    test_parse_scroll();
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
