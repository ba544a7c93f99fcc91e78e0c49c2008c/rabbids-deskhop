/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */

#include "inject.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* HID usage IDs (keyboard/keypad page) we need, kept local so this module stays
   free of any TinyUSB dependency. */
enum {
    KC_A = 0x04, KC_1 = 0x1E, KC_0 = 0x27,
    KC_ENTER = 0x28, KC_ESC = 0x29, KC_BKSP = 0x2A, KC_TAB = 0x2B, KC_SPACE = 0x2C,
    KC_MINUS = 0x2D, KC_EQUAL = 0x2E, KC_LBRACKET = 0x2F, KC_RBRACKET = 0x30,
    KC_BACKSLASH = 0x31, KC_SEMICOLON = 0x33, KC_APOSTROPHE = 0x34, KC_GRAVE = 0x35,
    KC_COMMA = 0x36, KC_PERIOD = 0x37, KC_SLASH = 0x38, KC_CAPS = 0x39,
    KC_F1 = 0x3A, KC_INSERT = 0x49, KC_HOME = 0x4A, KC_PGUP = 0x4B, KC_DELETE = 0x4C,
    KC_END = 0x4D, KC_PGDN = 0x4E, KC_RIGHT = 0x4F, KC_LEFT = 0x50, KC_DOWN = 0x51, KC_UP = 0x52,
};

enum {
    MOD_CTRL  = INJECT_MOD_CTRL,
    MOD_SHIFT = INJECT_MOD_SHIFT,
    MOD_ALT   = INJECT_MOD_ALT,
    MOD_GUI   = INJECT_MOD_GUI,
};

bool inject_ascii_to_hid(char c, uint8_t *keycode, bool *shift) {
    *shift = false;

    if (c >= 'a' && c <= 'z') { *keycode = KC_A + (c - 'a'); return true; }
    if (c >= 'A' && c <= 'Z') { *keycode = KC_A + (c - 'A'); *shift = true; return true; }
    if (c >= '1' && c <= '9') { *keycode = KC_1 + (c - '1'); return true; }
    if (c == '0')             { *keycode = KC_0; return true; }
    if (c == ' ')             { *keycode = KC_SPACE; return true; }

    /* Unshifted punctuation. */
    switch (c) {
        case '-':  *keycode = KC_MINUS; return true;
        case '=':  *keycode = KC_EQUAL; return true;
        case '[':  *keycode = KC_LBRACKET; return true;
        case ']':  *keycode = KC_RBRACKET; return true;
        case '\\': *keycode = KC_BACKSLASH; return true;
        case ';':  *keycode = KC_SEMICOLON; return true;
        case '\'': *keycode = KC_APOSTROPHE; return true;
        case '`':  *keycode = KC_GRAVE; return true;
        case ',':  *keycode = KC_COMMA; return true;
        case '.':  *keycode = KC_PERIOD; return true;
        case '/':  *keycode = KC_SLASH; return true;
    }

    /* Shifted symbols. */
    *shift = true;
    switch (c) {
        case '!': *keycode = KC_1; return true;
        case '@': *keycode = KC_1 + 1; return true;
        case '#': *keycode = KC_1 + 2; return true;
        case '$': *keycode = KC_1 + 3; return true;
        case '%': *keycode = KC_1 + 4; return true;
        case '^': *keycode = KC_1 + 5; return true;
        case '&': *keycode = KC_1 + 6; return true;
        case '*': *keycode = KC_1 + 7; return true;
        case '(': *keycode = KC_1 + 8; return true;
        case ')': *keycode = KC_0; return true;
        case '_': *keycode = KC_MINUS; return true;
        case '+': *keycode = KC_EQUAL; return true;
        case '{': *keycode = KC_LBRACKET; return true;
        case '}': *keycode = KC_RBRACKET; return true;
        case '|': *keycode = KC_BACKSLASH; return true;
        case ':': *keycode = KC_SEMICOLON; return true;
        case '"': *keycode = KC_APOSTROPHE; return true;
        case '~': *keycode = KC_GRAVE; return true;
        case '<': *keycode = KC_COMMA; return true;
        case '>': *keycode = KC_PERIOD; return true;
        case '?': *keycode = KC_SLASH; return true;
    }

    *shift = false;
    return false;
}

/* Case-insensitive compare of a NUL-terminated name against a literal. */
static bool name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

bool inject_keyname(const char *name, uint8_t *code, bool *is_modifier) {
    *is_modifier = false;

    if (name[0] == '\0')
        return false;

    /* Single printable character -> ASCII mapping (lets `key a`, `key /` work). */
    if (name[1] == '\0') {
        bool shift;
        return inject_ascii_to_hid(name[0], code, &shift);
    }

    /* Modifiers. "primary" mirrors remote-hid's portable modifier; on the PC
       targets this project drives it is Ctrl (there is no host-OS probe here). */
    if (name_eq(name, "ctrl") || name_eq(name, "control") || name_eq(name, "primary"))
                                                           { *code = MOD_CTRL;  *is_modifier = true; return true; }
    if (name_eq(name, "shift"))                            { *code = MOD_SHIFT; *is_modifier = true; return true; }
    if (name_eq(name, "alt"))                              { *code = MOD_ALT;   *is_modifier = true; return true; }
    if (name_eq(name, "gui") || name_eq(name, "win") || name_eq(name, "cmd") || name_eq(name, "super"))
                                                           { *code = MOD_GUI;   *is_modifier = true; return true; }

    /* Named keys. */
    if (name_eq(name, "enter") || name_eq(name, "return")) { *code = KC_ENTER; return true; }
    if (name_eq(name, "esc") || name_eq(name, "escape"))   { *code = KC_ESC; return true; }
    if (name_eq(name, "tab"))                              { *code = KC_TAB; return true; }
    if (name_eq(name, "space"))                            { *code = KC_SPACE; return true; }
    if (name_eq(name, "bksp") || name_eq(name, "backspace")) { *code = KC_BKSP; return true; }
    if (name_eq(name, "del") || name_eq(name, "delete"))   { *code = KC_DELETE; return true; }
    if (name_eq(name, "ins") || name_eq(name, "insert"))   { *code = KC_INSERT; return true; }
    if (name_eq(name, "home"))                             { *code = KC_HOME; return true; }
    if (name_eq(name, "end"))                              { *code = KC_END; return true; }
    if (name_eq(name, "pgup"))                             { *code = KC_PGUP; return true; }
    if (name_eq(name, "pgdn"))                             { *code = KC_PGDN; return true; }
    if (name_eq(name, "up"))                               { *code = KC_UP; return true; }
    if (name_eq(name, "down"))                             { *code = KC_DOWN; return true; }
    if (name_eq(name, "left"))                             { *code = KC_LEFT; return true; }
    if (name_eq(name, "right"))                            { *code = KC_RIGHT; return true; }
    if (name_eq(name, "caps") || name_eq(name, "capslock")) { *code = KC_CAPS; return true; }

    /* Function keys f1..f12. */
    if ((name[0] == 'f' || name[0] == 'F')) {
        int n = 0;
        const char *p = name + 1;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (*p == '\0' && n >= 1 && n <= 12) { *code = KC_F1 + (n - 1); return true; }
    }

    return false;
}

/* --- line parsing helpers ---------------------------------------------- */

static const char *skip_spaces(const char *p) {
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Copy the next whitespace-delimited token into buf (NUL-terminated, bounded);
   returns the position just after the token. */
static const char *next_token(const char *p, char *buf, size_t buflen) {
    p = skip_spaces(p);
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (i + 1 < buflen)
            buf[i++] = *p;
        p++;
    }
    buf[i] = '\0';
    return p;
}

/* Parse a signed decimal integer token; returns false if not a valid number. */
static bool parse_int(const char *tok, long *out) {
    if (*tok == '\0')
        return false;
    bool neg = false;
    if (*tok == '-') { neg = true; tok++; }
    else if (*tok == '+') { tok++; }
    if (*tok == '\0')
        return false;
    long v = 0;
    for (; *tok; tok++) {
        if (*tok < '0' || *tok > '9')
            return false;
        /* Bound to the int32 range so a value never wraps a 32-bit long on the
           Pico (where it could otherwise slip past a later range check) and the
           accept/reject boundary is identical on the 64-bit host test build. */
        if (v > (INT32_MAX - 9) / 10)
            return false;
        v = v * 10 + (*tok - '0');
    }
    *out = neg ? -v : v;
    return true;
}

/* Parse a chord like "ctrl+alt+del" or "a" into modifier bits + one keycode.
   With allow_mod_only, a modifier-only chord ("ctrl") is accepted (for the
   hold/release verbs); otherwise exactly one non-modifier key is required. */
static bool parse_chord(const char *tok, uint8_t *modifier, uint8_t *keycode, bool allow_mod_only) {
    char part[16];
    *modifier = 0;
    *keycode  = 0;
    bool have_key = false;
    bool have_any = false;

    while (*tok) {
        size_t i = 0;
        while (*tok && *tok != '+') {
            if (i + 1 < sizeof(part))
                part[i++] = *tok;
            tok++;
        }
        part[i] = '\0';
        if (*tok == '+')
            tok++;

        uint8_t code;
        bool is_mod;
        if (!inject_keyname(part, &code, &is_mod))
            return false;
        have_any = true;
        if (is_mod) {
            *modifier |= code;
        } else {
            if (have_key)
                return false; /* only one non-modifier key per chord */
            *keycode = code;
            have_key = true;
        }
    }

    if (have_key)
        return true;
    return allow_mod_only && have_any; /* modifier-only allowed only for hold/release */
}

/* Match a mouse button name; returns true and sets *btn on left/right/middle. */
static bool parse_button(const char *tok, uint8_t *btn) {
    if (name_eq(tok, "left"))   { *btn = INJECT_BTN_LEFT;   return true; }
    if (name_eq(tok, "right"))  { *btn = INJECT_BTN_RIGHT;  return true; }
    if (name_eq(tok, "middle")) { *btn = INJECT_BTN_MIDDLE; return true; }
    return false;
}

/* Parse the optional trailing tokens shared by `click` and `drag`: a button
   name, a repeat count (digits, click only), and/or a modifier-only chord held
   for the action. Tokens may appear in any order. Returns false on an
   unrecognized token. The button, count and mods outputs must be
   pre-initialized by the caller. */
static bool parse_mouse_opts(const char *rest, uint8_t *button, uint8_t *count,
                             uint8_t *mods, bool allow_count) {
    char tok[24];
    while (1) {
        rest = next_token(rest, tok, sizeof(tok));
        if (tok[0] == '\0')
            return true;

        if (parse_button(tok, button))
            continue;

        if (allow_count && count && tok[0] >= '0' && tok[0] <= '9') {
            long n;
            if (!parse_int(tok, &n) || n < 1 || n > INJECT_MAX_COUNT)
                return false;
            *count = (uint8_t)n;
            continue;
        }

        /* Otherwise it must be a modifier-only chord (held for the action). */
        uint8_t m, k;
        if (!parse_chord(tok, &m, &k, true) || k != 0)
            return false;
        *mods |= m;
    }
}

void inject_parse_line(const char *line, inject_cmd_t *cmd) {
    memset(cmd, 0, sizeof(*cmd));

    const char *p = skip_spaces(line);
    if (*p == '\0' || *p == '#') {
        cmd->kind = INJ_NONE;
        return;
    }

    char verb[12];
    const char *rest = next_token(p, verb, sizeof(verb));

    if (name_eq(verb, "type")) {
        /* The literal is the remainder after exactly one separating space. */
        if (*rest == ' ' || *rest == '\t')
            rest++;
        /* Reject rather than silently truncate: the caller gets "err" and can
           split the text instead of believing a cut string was typed. */
        if (strlen(rest) > INJECT_MAX_TEXT - 1) {
            cmd->kind = INJ_ERROR;
            return;
        }
        size_t i = 0;
        while (rest[i]) {
            cmd->text[i] = rest[i];
            i++;
        }
        cmd->text[i] = '\0';
        cmd->kind = INJ_TYPE;
        return;
    }

    /* shortcut: one or more space-separated chords, tapped in order
       (remote-hid keyboard_shortcut, e.g. "ctrl+k ctrl+s"). */
    if (name_eq(verb, "shortcut")) {
        char tok[24];
        uint8_t n = 0;
        while (1) {
            rest = next_token(rest, tok, sizeof(tok));
            if (tok[0] == '\0')
                break;
            if (n >= INJECT_MAX_CHORDS) { cmd->kind = INJ_ERROR; return; }
            if (!parse_chord(tok, &cmd->chords[n].modifier, &cmd->chords[n].keycode, false)) {
                cmd->kind = INJ_ERROR;
                return;
            }
            n++;
        }
        if (n == 0) { cmd->kind = INJ_ERROR; return; }
        cmd->nchords = n;
        cmd->kind    = INJ_SHORTCUT;
        return;
    }

    /* key / key_down / key_up: a single chord (remote-hid keyboard_key). */
    if (name_eq(verb, "key")
        || name_eq(verb, "key_down") || name_eq(verb, "down")
        || name_eq(verb, "key_up") || name_eq(verb, "up")) {
        char tok[24];
        next_token(rest, tok, sizeof(tok));
        bool hold = name_eq(verb, "key_down") || name_eq(verb, "down")
                 || name_eq(verb, "key_up") || name_eq(verb, "up");
        if (!parse_chord(tok, &cmd->modifier, &cmd->keycode, hold)) {
            cmd->kind = INJ_ERROR;
            return;
        }
        if (name_eq(verb, "key_down") || name_eq(verb, "down"))
            cmd->kind = INJ_KEY_DOWN;
        else if (name_eq(verb, "key_up") || name_eq(verb, "up"))
            cmd->kind = INJ_KEY_UP;
        else
            cmd->kind = INJ_KEY;
        return;
    }

    if (name_eq(verb, "release_all") || name_eq(verb, "release")) {
        cmd->kind = INJ_RELEASE_ALL;
        return;
    }

    if (name_eq(verb, "move") || name_eq(verb, "moveto")) {
        char a[12], b[12];
        rest = next_token(rest, a, sizeof(a));
        next_token(rest, b, sizeof(b));
        long x, y;
        bool abs = name_eq(verb, "moveto");
        long lo = abs ? 0 : INJECT_COORD_MIN;
        long hi = abs ? INJECT_ABS_MAX : INJECT_COORD_MAX;
        if (parse_int(a, &x) && parse_int(b, &y) &&
            x >= lo && x <= hi && y >= lo && y <= hi) {
            cmd->x = (int16_t)x;
            cmd->y = (int16_t)y;
            cmd->kind = abs ? INJ_MOVETO : INJ_MOVE;
        } else {
            cmd->kind = INJ_ERROR;
        }
        return;
    }

    /* click [button] [count] [modifiers] (remote-hid mouse_click). */
    if (name_eq(verb, "click")) {
        cmd->buttons = INJECT_BTN_LEFT;
        cmd->count   = 1;
        if (!parse_mouse_opts(rest, &cmd->buttons, &cmd->count, &cmd->modifier, true))
            cmd->kind = INJ_ERROR;
        else
            cmd->kind = INJ_CLICK;
        return;
    }

    /* mouse_down / mouse_up [button] (remote-hid mouse_down/mouse_up). */
    if (name_eq(verb, "mouse_down") || name_eq(verb, "mdown")
        || name_eq(verb, "mouse_up") || name_eq(verb, "mup")) {
        char tok[12];
        next_token(rest, tok, sizeof(tok));
        cmd->buttons = INJECT_BTN_LEFT;
        if (tok[0] != '\0' && !parse_button(tok, &cmd->buttons)) {
            cmd->kind = INJ_ERROR;
            return;
        }
        cmd->kind = (name_eq(verb, "mouse_down") || name_eq(verb, "mdown"))
                        ? INJ_MOUSE_DOWN : INJ_MOUSE_UP;
        return;
    }

    /* drag <dx> <dy> [button] [modifiers] (remote-hid mouse_drag). */
    if (name_eq(verb, "drag")) {
        char a[12], b[12];
        rest = next_token(rest, a, sizeof(a));
        rest = next_token(rest, b, sizeof(b));
        long x, y;
        if (!parse_int(a, &x) || !parse_int(b, &y) ||
            x < INJECT_COORD_MIN || x > INJECT_COORD_MAX ||
            y < INJECT_COORD_MIN || y > INJECT_COORD_MAX) {
            cmd->kind = INJ_ERROR;
            return;
        }
        cmd->x       = (int16_t)x;
        cmd->y       = (int16_t)y;
        cmd->buttons = INJECT_BTN_LEFT;
        if (!parse_mouse_opts(rest, &cmd->buttons, NULL, &cmd->modifier, false))
            cmd->kind = INJ_ERROR;
        else
            cmd->kind = INJ_DRAG;
        return;
    }

    /* scroll <amount> [modifiers] (remote-hid mouse_scroll). */
    if (name_eq(verb, "scroll")) {
        char tok[12];
        rest = next_token(rest, tok, sizeof(tok));
        long n;
        if (!parse_int(tok, &n) || n < -INJECT_SCROLL_MAX || n > INJECT_SCROLL_MAX) {
            cmd->kind = INJ_ERROR;
            return;
        }
        cmd->y = (int16_t)n;
        uint8_t dummy_btn = 0, dummy_cnt = 0;
        if (!parse_mouse_opts(rest, &dummy_btn, &dummy_cnt, &cmd->modifier, false) || dummy_btn != 0)
            cmd->kind = INJ_ERROR;
        else
            cmd->kind = INJ_SCROLL;
        return;
    }

    if (name_eq(verb, "delay") || name_eq(verb, "sleep")) {
        char tok[12];
        next_token(rest, tok, sizeof(tok));
        long ms;
        if (parse_int(tok, &ms) && ms >= 0) { cmd->ms = (uint32_t)ms; cmd->kind = INJ_DELAY; }
        else cmd->kind = INJ_ERROR;
        return;
    }

    if (name_eq(verb, "target")) {
        char tok[12];
        next_token(rest, tok, sizeof(tok));
        if (name_eq(tok, "local"))       { cmd->target = INJECT_TARGET_LOCAL;  cmd->kind = INJ_TARGET; }
        else if (name_eq(tok, "remote")) { cmd->target = INJECT_TARGET_REMOTE; cmd->kind = INJ_TARGET; }
        else cmd->kind = INJ_ERROR;
        return;
    }

    cmd->kind = INJ_ERROR;
}
