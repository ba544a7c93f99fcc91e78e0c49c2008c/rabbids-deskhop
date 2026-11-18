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

/* Executor for the COM-port injection feature: reads command lines from the CDC
 * (serial) interface, parses them with the pure inject.c parser, expands them
 * into timed HID "steps", and emits those either to this board's own PC
 * (target local) or to the other PC over UART (target remote), where the
 * existing KEYBOARD/MOUSE_REPORT_MSG handlers inject them. Compiled as a no-op
 * unless the board is built with DH_INJECT (so the target board stays HID-only).
 */

#include "main.h"

#ifdef DH_INJECT

#include "inject.h"

#include <stdio.h>

/* Bumped when a line is refused for backpressure (queue full) or because it was
   too long; surfaced by /status so an operator/script can see dropped input. */
static uint32_t dropped_lines;

#define INJECT_PROTO_VERSION 1

#define STEP_KBD   0
#define STEP_MOUSE 1
#define STEP_DELAY 2

#define INJECT_RING    256   /* max queued low-level steps */
#define INJECT_LINEBUF 128   /* max command line length */
#define INTER_STEP_US  4000  /* spacing between emitted HID reports (~250/s) */

/* Worst-case low-level steps any single command line can expand into: a `type`
   taps each character (press+release). No line may need more, so if this many
   slots are free the whole line fits and expansion is never truncated mid-way
   (which could leave a key/modifier pressed but never released). */
#define INJECT_MAX_STEPS_PER_LINE (2 * (INJECT_MAX_TEXT - 1))

typedef struct {
    uint8_t kind;
    uint8_t target;
    union {
        hid_keyboard_report_t kbd;
        mouse_report_t        mouse;
        uint32_t              delay_ms;
    } u;
} inject_step_t;

static inject_step_t ring[INJECT_RING];
static uint16_t ring_head, ring_tail;
static uint64_t inject_gate_us;
static uint8_t  cur_target = INJECT_TARGET_REMOTE;

static char     linebuf[INJECT_LINEBUF];
static uint16_t linelen;

/* Keyboard keys/modifiers and mouse buttons currently held across sequence
   steps (expansion-time baseline). Emitted reports are absolute, so the host
   state follows this. */
static uint8_t held_mod;
static uint8_t held_keys[6];
static uint8_t held_btn;

static bool ring_push(const inject_step_t *s) {
    uint16_t next = (uint16_t)((ring_head + 1) % INJECT_RING);
    if (next == ring_tail)
        return false; /* full: drop (the line was too long for the queue) */
    ring[ring_head] = *s;
    ring_head = next;
    return true;
}

/* Number of free slots in the ring (capacity is INJECT_RING - 1). */
static uint16_t ring_free(void) {
    return (uint16_t)((ring_tail - ring_head - 1 + INJECT_RING) % INJECT_RING);
}

/* Number of steps currently queued (pending emission). */
static uint16_t ring_count(void) {
    return (uint16_t)((ring_head - ring_tail + INJECT_RING) % INJECT_RING);
}

static bool ring_pop(inject_step_t *s) {
    if (ring_tail == ring_head)
        return false;
    *s = ring[ring_tail];
    ring_tail = (uint16_t)((ring_tail + 1) % INJECT_RING);
    return true;
}

/* Queue a keyboard report for the given absolute modifier + key set. */
static void emit_kbd(uint8_t modifier, const uint8_t keys[6]) {
    inject_step_t s = {.kind = STEP_KBD, .target = cur_target};
    s.u.kbd.modifier = modifier;
    for (int i = 0; i < 6; i++)
        s.u.kbd.keycode[i] = keys[i];
    ring_push(&s);
}

static void keys_add(uint8_t keys[6], uint8_t kc) {
    if (kc == 0)
        return;
    for (int i = 0; i < 6; i++)
        if (keys[i] == kc)
            return; /* already held */
    for (int i = 0; i < 6; i++)
        if (keys[i] == 0) {
            keys[i] = kc;
            return;
        }
    /* 6-key rollover full: drop silently */
}

static void keys_remove(uint8_t keys[6], uint8_t kc) {
    for (int i = 0; i < 6; i++)
        if (keys[i] == kc)
            keys[i] = 0;
}

/* Emit a momentary chord on top of the held baseline, then return to baseline. */
static void tap_chord(uint8_t modifier, uint8_t keycode) {
    uint8_t tmp[6];
    memcpy(tmp, held_keys, sizeof(tmp));
    keys_add(tmp, keycode);
    emit_kbd(held_mod | modifier, tmp);
    emit_kbd(held_mod, held_keys); /* release just the tapped keys/mods */
}

static void push_mouse(uint8_t mode, uint8_t buttons, int16_t x, int16_t y, int8_t wheel) {
    inject_step_t s = {.kind = STEP_MOUSE, .target = cur_target};
    s.u.mouse.mode    = mode;
    s.u.mouse.buttons = buttons;
    s.u.mouse.x       = x;
    s.u.mouse.y       = y;
    s.u.mouse.wheel   = wheel;
    ring_push(&s);
}

static void push_delay(uint32_t ms) {
    inject_step_t s = {.kind = STEP_DELAY, .target = cur_target};
    s.u.delay_ms = ms;
    ring_push(&s);
}

/* Momentarily press extra modifiers on top of the held keyboard baseline
   (for a click/scroll/drag with modifiers held), returning to baseline. */
static void mods_press(uint8_t mods) {
    if (mods)
        emit_kbd(held_mod | mods, held_keys);
}
static void mods_release(uint8_t mods) {
    if (mods)
        emit_kbd(held_mod, held_keys);
}

/* Expand a parsed command into low-level steps. Returns false on a parse error. */
static bool inject_exec_cmd(const inject_cmd_t *c) {
    switch (c->kind) {
        case INJ_TYPE:
            for (const char *p = c->text; *p; p++) {
                uint8_t kc;
                bool shift;
                if (!inject_ascii_to_hid(*p, &kc, &shift))
                    continue; /* silently skip characters with no US-layout key */
                tap_chord(shift ? INJECT_MOD_SHIFT : 0, kc);
            }
            return true;

        case INJ_KEY:
            tap_chord(c->modifier, c->keycode);
            return true;

        case INJ_SHORTCUT:
            for (uint8_t i = 0; i < c->nchords; i++)
                tap_chord(c->chords[i].modifier, c->chords[i].keycode);
            return true;

        case INJ_KEY_DOWN:
            held_mod |= c->modifier;
            keys_add(held_keys, c->keycode);
            emit_kbd(held_mod, held_keys);
            return true;

        case INJ_KEY_UP:
            held_mod &= (uint8_t)~c->modifier;
            keys_remove(held_keys, c->keycode);
            emit_kbd(held_mod, held_keys);
            return true;

        case INJ_RELEASE_ALL:
            held_mod = 0;
            held_btn = 0;
            memset(held_keys, 0, sizeof(held_keys));
            emit_kbd(0, held_keys);                 /* release all keys */
            push_mouse(RELATIVE, 0, 0, 0, 0);       /* release all mouse buttons */
            return true;

        case INJ_MOVE:
            /* Keep any held button down so a mouse_down + move composes a drag. */
            push_mouse(RELATIVE, held_btn, c->x, c->y, 0);
            return true;

        case INJ_MOVETO:
            push_mouse(ABSOLUTE, held_btn, c->x, c->y, 0);
            return true;

        case INJ_CLICK: {
            uint8_t n = c->count ? c->count : 1;
            mods_press(c->modifier);
            for (uint8_t i = 0; i < n; i++) {
                push_mouse(RELATIVE, held_btn | c->buttons, 0, 0, 0); /* press */
                push_mouse(RELATIVE, held_btn, 0, 0, 0);              /* release */
            }
            mods_release(c->modifier);
            return true;
        }

        case INJ_MOUSE_DOWN:
            held_btn |= c->buttons;
            push_mouse(RELATIVE, held_btn, 0, 0, 0);
            return true;

        case INJ_MOUSE_UP:
            held_btn &= (uint8_t)~c->buttons;
            push_mouse(RELATIVE, held_btn, 0, 0, 0);
            return true;

        case INJ_DRAG:
            mods_press(c->modifier);
            push_mouse(RELATIVE, held_btn | c->buttons, 0, 0, 0);       /* button down */
            push_mouse(RELATIVE, held_btn | c->buttons, c->x, c->y, 0); /* move held */
            push_mouse(RELATIVE, held_btn, 0, 0, 0);                    /* button up */
            mods_release(c->modifier);
            return true;

        case INJ_SCROLL:
            mods_press(c->modifier);
            push_mouse(RELATIVE, held_btn, 0, 0, (int8_t)c->y);
            mods_release(c->modifier);
            return true;

        case INJ_DELAY:
            push_delay(c->ms);
            return true;

        case INJ_TARGET:
            cur_target = c->target;
            return true;

        case INJ_NONE:
            return true;

        case INJ_ERROR:
        default:
            return false;
    }
}

/* --- control plane: /help, /contract, /status, /version ---------------- */

/* Case-insensitive match of a control-command word; the word ends at the first
   space/CR or the string end, so trailing arguments are ignored. */
static bool meta_eq(const char *a, const char *lit) {
    while (*lit) {
        char ca = *a, cl = *lit;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cl >= 'A' && cl <= 'Z') cl += 32;
        if (ca != cl)
            return false;
        a++; lit++;
    }
    return *a == '\0' || *a == ' ' || *a == '\t' || *a == '\r';
}

/* Human-readable command reference (the "usage contract"). */
static void meta_help(uint8_t itf) {
    tud_cdc_n_write_str(itf,
        "DeskHop COM injection commands:\r\n"
        "  type <text>                    literal ASCII (US layout)\r\n"
        "  key <chord>                    tap one chord, e.g. ctrl+alt+del\r\n"
        "  shortcut <chord> [chord...]    tap chords in order, e.g. ctrl+k ctrl+s\r\n"
        "  key_down|down <chord>          press and hold\r\n"
        "  key_up|up <chord>              release\r\n"
        "  release_all|release            release all keys and buttons\r\n"
        "  move <dx> <dy>                 relative move (-32767..32767)\r\n"
        "  moveto <x> <y>                 absolute move (0..32767)\r\n"
        "  click [button] [count] [mods]  count 1..9, mods held for the click\r\n"
        "  mouse_down|mdown [button]      hold a mouse button\r\n"
        "  mouse_up|mup [button]          release a mouse button\r\n"
        "  drag <dx> <dy> [button] [mods] press, move, release\r\n"
        "  scroll <n> [mods]              wheel, n=-127..127\r\n"
        "  delay <ms>                     wait milliseconds\r\n"
        "  target <local|remote>          where to inject (default remote)\r\n"
        "button: left|right|middle   mods: ctrl(primary) shift alt gui(win|cmd), joined with +\r\n"
        "control: /help /contract /status /version   ('ok' = queued, poll /status for drain)\r\n");
}

/* Compact, machine-parsable grammar: a count line, then one line per verb. */
static void meta_contract(uint8_t itf) {
    tud_cdc_n_write_str(itf,
        "contract 15\r\n"
        "type <text>\r\n"
        "key <chord>\r\n"
        "shortcut <chord>...\r\n"
        "key_down <chord>\r\n"
        "key_up <chord>\r\n"
        "release_all\r\n"
        "move <dx> <dy>\r\n"
        "moveto <x> <y>\r\n"
        "click [button] [count] [mods]\r\n"
        "mouse_down [button]\r\n"
        "mouse_up [button]\r\n"
        "drag <dx> <dy> [button] [mods]\r\n"
        "scroll <n> [mods]\r\n"
        "delay <ms>\r\n"
        "target local|remote\r\n");
}

/* One-line live snapshot of the injector state. */
static void meta_status(uint8_t itf) {
    uint8_t nkeys = 0;
    for (int i = 0; i < 6; i++)
        if (held_keys[i])
            nkeys++;

    char line[128];
    snprintf(line, sizeof(line),
             "status target=%s queued=%u held_mod=0x%02X held_keys=%u held_btn=0x%02X dropped=%lu\r\n",
             cur_target == INJECT_TARGET_REMOTE ? "remote" : "local",
             (unsigned)ring_count(), (unsigned)held_mod, (unsigned)nkeys,
             (unsigned)held_btn, (unsigned long)dropped_lines);
    tud_cdc_n_write_str(itf, line);
}

/* Dispatch a control line (starts with '/'). Returns true if it was one. These
   never inject and always answer, even when the injection queue is full. */
static bool inject_meta(uint8_t itf, const char *line) {
    if (*line != '/')
        return false;

    const char *c = line + 1;
    if (meta_eq(c, "help") || meta_eq(c, "?")) {
        meta_help(itf);
    } else if (meta_eq(c, "contract")) {
        meta_contract(itf);
    } else if (meta_eq(c, "status")) {
        meta_status(itf);
    } else if (meta_eq(c, "version")) {
        char line2[48];
        snprintf(line2, sizeof(line2), "DeskHop-inject %d\r\n", INJECT_PROTO_VERSION);
        tud_cdc_n_write_str(itf, line2);
    } else {
        tud_cdc_n_write_str(itf, "err unknown control command (try /help)\r\n");
    }
    tud_cdc_n_write_flush(itf);
    return true;
}

/* Process one finished command line. */
static void inject_handle_line(uint8_t itf) {
    linebuf[linelen] = '\0';

    /* Skip leading whitespace so control commands are recognized when indented. */
    const char *line = linebuf;
    while (*line == ' ' || *line == '\t')
        line++;

    /* Control plane (/help, /status, ...) answers locally and injects nothing. */
    if (inject_meta(itf, line)) {
        linelen = 0;
        return;
    }

    inject_cmd_t cmd;
    inject_parse_line(line, &cmd);

    /* Admit a command only when the whole line is guaranteed to fit, so a chord
       can never be queued with its release dropped on overflow (stuck key). If
       the queue is backed up, refuse the line ("err") instead of truncating it;
       the operator retries once the backlog drains. */
    bool ok;
    if (cmd.kind == INJ_ERROR) {
        ok = false;
    } else if (ring_free() < INJECT_MAX_STEPS_PER_LINE) {
        ok = false; /* backpressure: queue too full to admit atomically */
        dropped_lines++;
    } else {
        ok = inject_exec_cmd(&cmd);
    }

    const char *reply = ok ? "ok\r\n" : "err\r\n";
    tud_cdc_n_write_str(itf, reply);
    tud_cdc_n_write_flush(itf);

    linelen = 0;
}

/* TinyUSB callback: new bytes arrived on the CDC interface. */
void tud_cdc_rx_cb(uint8_t itf) {
    static bool line_overflow; /* true once a line exceeded the buffer */
    uint8_t buf[64];
    uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));

    for (uint32_t i = 0; i < count; i++) {
        char ch = (char)buf[i];
        if (ch == '\n' || ch == '\r') {
            if (line_overflow) {
                /* The whole overlong line is dropped, not parsed as fragments. */
                tud_cdc_n_write_str(itf, "err\r\n");
                tud_cdc_n_write_flush(itf);
                dropped_lines++;
                line_overflow = false;
                linelen = 0;
            } else if (linelen > 0) {
                inject_handle_line(itf);
            }
        } else if (line_overflow) {
            /* Keep discarding until the newline that closes the overlong line. */
        } else if (linelen < INJECT_LINEBUF - 1) {
            linebuf[linelen++] = ch;
        } else {
            line_overflow = true; /* overlong: discard the rest of this line */
        }
    }
}

/* Scheduler task: emit queued steps, honoring inter-step spacing and delays. */
void inject_task(device_t *state) {
    if (time_us_64() < inject_gate_us)
        return;

    inject_step_t s;
    if (!ring_pop(&s))
        return;

    uint64_t now = time_us_64();

    switch (s.kind) {
        case STEP_DELAY:
            inject_gate_us = now + (uint64_t)s.u.delay_ms * 1000;
            break;

        case STEP_KBD:
            if (s.target == INJECT_TARGET_LOCAL)
                queue_kbd_report(&s.u.kbd, state);
            else
                queue_packet((uint8_t *)&s.u.kbd, KEYBOARD_REPORT_MSG, KBD_REPORT_LENGTH);
            inject_gate_us = now + INTER_STEP_US;
            break;

        case STEP_MOUSE:
            if (s.target == INJECT_TARGET_LOCAL)
                queue_mouse_report(&s.u.mouse, state);
            else
                queue_packet((uint8_t *)&s.u.mouse, MOUSE_REPORT_MSG, MOUSE_REPORT_LENGTH);
            inject_gate_us = now + INTER_STEP_US;
            break;
    }
}

#endif /* DH_INJECT */
