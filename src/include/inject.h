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
#pragma once

/* Pure, hardware-independent parser for the COM-port injection command language.
 * It turns one text command line into a structured inject_cmd_t; the executor
 * (inject_exec.c) is what actually emits HID reports. No pico-sdk / TinyUSB
 * dependency, so it can be unit-tested on the host (see tests/). */

#include <stdbool.h>
#include <stdint.h>

#define INJECT_MAX_TEXT   48 /* max literal length for a `type` command (incl. NUL) */
#define INJECT_MAX_CHORDS 8  /* max chords in one `shortcut` line */
#define INJECT_MAX_COUNT  9  /* max click repeat count */

/* Coordinate / wheel limits, validated by the parser so values never wrap when
   narrowed to the HID report fields (int16 position, int8 wheel). */
#define INJECT_COORD_MIN  (-32767) /* relative move / drag delta */
#define INJECT_COORD_MAX  32767
#define INJECT_ABS_MAX    32767    /* absolute moveto range is 0..INJECT_ABS_MAX */
#define INJECT_SCROLL_MAX 127      /* wheel field is int8 */

/* HID modifier bitmask (USB HID keyboard page), shared with the executor so the
   bit values are defined in exactly one place. */
#define INJECT_MOD_CTRL   0x01
#define INJECT_MOD_SHIFT  0x02
#define INJECT_MOD_ALT    0x04
#define INJECT_MOD_GUI    0x08

/* HID mouse button bitmask (matches the USB HID boot mouse / DeskHop report). */
#define INJECT_BTN_LEFT   0x01
#define INJECT_BTN_RIGHT  0x02
#define INJECT_BTN_MIDDLE 0x04

/* Step vocabulary, aligned with the remote-hid "sequence" step names so a
   sequence reads the same on both projects. A sequence is just several of
   these command lines executed in order. */
typedef enum {
    INJ_NONE = 0,    /* blank line or comment: nothing to do */
    INJ_ERROR,       /* could not be parsed */
    INJ_TYPE,        /* type literal ASCII text (cmd.text) */
    INJ_KEY,         /* tap a key chord: press then release (cmd.modifier + cmd.keycode) */
    INJ_SHORTCUT,    /* tap a sequence of chords in order (cmd.chords[0..nchords)) */
    INJ_KEY_DOWN,    /* press and HOLD a chord (cmd.modifier + cmd.keycode) */
    INJ_KEY_UP,      /* release a specific chord (cmd.modifier + cmd.keycode) */
    INJ_RELEASE_ALL, /* release every held key / button */
    INJ_MOVE,        /* relative mouse move (cmd.x, cmd.y) */
    INJ_MOVETO,      /* absolute mouse move, 0..32767 (cmd.x, cmd.y) */
    INJ_CLICK,       /* mouse click (cmd.buttons, cmd.count repeats, cmd.modifier held) */
    INJ_MOUSE_DOWN,  /* press and HOLD a mouse button (cmd.buttons) */
    INJ_MOUSE_UP,    /* release a held mouse button (cmd.buttons) */
    INJ_DRAG,        /* press cmd.buttons, relative move (cmd.x,cmd.y) with it held, release */
    INJ_SCROLL,      /* mouse wheel, signed steps (cmd.y, cmd.modifier held) */
    INJ_DELAY,       /* wait cmd.ms milliseconds */
    INJ_TARGET,      /* choose where to inject (cmd.target: 0 local PC, 1 other PC) */
} inject_kind_t;

enum { INJECT_TARGET_LOCAL = 0, INJECT_TARGET_REMOTE = 1 };

/* One chord = optional modifier bits + one keycode (INJ_SHORTCUT array member). */
typedef struct {
    uint8_t modifier;
    uint8_t keycode;
} inject_chord_t;

typedef struct {
    inject_kind_t kind;
    char     text[INJECT_MAX_TEXT]; /* INJ_TYPE */
    uint8_t  modifier;              /* INJ_KEY/UP/DOWN: chord modifiers; CLICK/SCROLL/DRAG: held modifiers */
    uint8_t  keycode;               /* INJ_KEY/UP/DOWN: HID keycode */
    inject_chord_t chords[INJECT_MAX_CHORDS]; /* INJ_SHORTCUT */
    uint8_t  nchords;               /* INJ_SHORTCUT: number of chords */
    int16_t  x;                     /* INJ_MOVE/INJ_MOVETO/INJ_DRAG */
    int16_t  y;                     /* INJ_MOVE/INJ_MOVETO/INJ_DRAG/INJ_SCROLL */
    uint8_t  buttons;               /* INJ_CLICK/INJ_DRAG/INJ_MOUSE_DOWN/INJ_MOUSE_UP */
    uint8_t  count;                 /* INJ_CLICK: repeat count (1..INJECT_MAX_COUNT) */
    uint32_t ms;                    /* INJ_DELAY */
    uint8_t  target;                /* INJ_TARGET */
} inject_cmd_t;

/* Map a printable ASCII char (US layout) to a HID keycode + whether Shift is
   needed. Returns false for characters with no single-key mapping. */
bool inject_ascii_to_hid(char c, uint8_t *keycode, bool *shift);

/* Map a key name to either a HID keycode or a modifier bitmask.
   Names: a-z, 0-9, "enter","esc","tab","space","bksp","del","up","down",
   "left","right","home","end","pgup","pgdn","ins","f1".."f12", and the
   modifiers "ctrl"/"primary","shift","alt","gui"/"win"/"cmd". Returns false if
   unknown.
   On success, *is_modifier tells whether *code is a modifier bitmask. */
bool inject_keyname(const char *name, uint8_t *code, bool *is_modifier);

/* Parse one command line into *cmd. A leading '#' or an empty line yields
   INJ_NONE; anything unrecognized yields INJ_ERROR. */
void inject_parse_line(const char *line, inject_cmd_t *cmd);
