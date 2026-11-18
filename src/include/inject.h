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

#define INJECT_MAX_TEXT 48 /* max literal length for a `type` command */

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
    INJ_KEY_DOWN,    /* press and HOLD a chord (cmd.modifier + cmd.keycode) */
    INJ_KEY_UP,      /* release a specific chord (cmd.modifier + cmd.keycode) */
    INJ_RELEASE_ALL, /* release every held key / button */
    INJ_MOVE,        /* relative mouse move (cmd.x, cmd.y) */
    INJ_MOVETO,      /* absolute mouse move, 0..32767 (cmd.x, cmd.y) */
    INJ_CLICK,       /* mouse click: press then release (cmd.buttons) */
    INJ_SCROLL,      /* mouse wheel, signed steps (cmd.y) */
    INJ_DELAY,       /* wait cmd.ms milliseconds */
    INJ_TARGET,      /* choose where to inject (cmd.target: 0 local PC, 1 other PC) */
} inject_kind_t;

enum { INJECT_TARGET_LOCAL = 0, INJECT_TARGET_REMOTE = 1 };

typedef struct {
    inject_kind_t kind;
    char     text[INJECT_MAX_TEXT]; /* INJ_TYPE */
    uint8_t  modifier;              /* INJ_KEY: HID modifier bitmask */
    uint8_t  keycode;               /* INJ_KEY: HID keycode */
    int16_t  x;                     /* INJ_MOVE/INJ_MOVETO */
    int16_t  y;                     /* INJ_MOVE/INJ_MOVETO/INJ_SCROLL */
    uint8_t  buttons;               /* INJ_CLICK */
    uint32_t ms;                    /* INJ_DELAY */
    uint8_t  target;                /* INJ_TARGET */
} inject_cmd_t;

/* Map a printable ASCII char (US layout) to a HID keycode + whether Shift is
   needed. Returns false for characters with no single-key mapping. */
bool inject_ascii_to_hid(char c, uint8_t *keycode, bool *shift);

/* Map a key name to either a HID keycode or a modifier bitmask.
   Names: a-z, 0-9, "enter","esc","tab","space","bksp","del","up","down",
   "left","right","home","end","pgup","pgdn","ins","f1".."f12", and the
   modifiers "ctrl","shift","alt","gui"/"win". Returns false if unknown.
   On success, *is_modifier tells whether *code is a modifier bitmask. */
bool inject_keyname(const char *name, uint8_t *code, bool *is_modifier);

/* Parse one command line into *cmd. A leading '#' or an empty line yields
   INJ_NONE; anything unrecognized yields INJ_ERROR. */
void inject_parse_line(const char *line, inject_cmd_t *cmd);
