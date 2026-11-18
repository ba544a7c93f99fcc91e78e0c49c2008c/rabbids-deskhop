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

/* Pure, hardware-independent state machine for the output-switching leader key
 * (tap/double-tap a dead key, then a selector). Kept free of pico-sdk / TinyUSB
 * dependencies so it can be unit-tested on the host (see tests/). */

#include <stdbool.h>
#include <stdint.h>

#define LEADER_KEYS 6 /* keycodes in a boot keyboard report */

/* Internal state values (exposed for tests / struct storage). */
enum {
    LEADER_IDLE = 0,  /* not in a sequence */
    LEADER_FIRST_TAP, /* first leader tap seen, waiting for the second */
    LEADER_ARMED,     /* leader confirmed, waiting for a selector */
    LEADER_SWALLOW,   /* selector fired, swallow held keys until released */
};

/* What the caller should do with the current report. */
typedef enum {
    LEADER_PASS = 0,     /* not consumed: process/forward the report normally */
    LEADER_CONSUME,      /* consumed: withhold the report (do not forward) */
    LEADER_SWITCH_A,     /* select output A (also consumes the report) */
    LEADER_SWITCH_B,     /* select output B (also consumes the report) */
    LEADER_SWITCH_TOGGLE,/* toggle output   (also consumes the report) */
} leader_result_t;

/* Minimal view of a keyboard report, decoupled from the TinyUSB type. */
typedef struct {
    uint8_t modifier;
    uint8_t keycode[LEADER_KEYS];
} leader_report_t;

/* Persistent state of the sequence (store one per keyboard pipeline). */
typedef struct {
    uint8_t  state;     /* one of LEADER_* */
    uint64_t time_us;   /* timestamp of the last transition */
    bool     prev_down; /* was the leader key held in the previous report */
} leader_state_t;

/* Configuration, injected so tests can pick keys/timeouts freely. */
typedef struct {
    uint8_t  leader_key;
    uint8_t  select_a;
    uint8_t  select_b;
    uint8_t  toggle;
    bool     double_tap;
    uint32_t tap_window_ms; /* max gap between the two taps (double-tap mode) */
    uint32_t timeout_ms;    /* how long the armed leader waits for a selector */
} leader_cfg_t;

/* Advance the state machine by one keyboard report. `now_us` is a monotonic
   microsecond clock. Returns what the caller should do with the report. */
leader_result_t leader_process(leader_state_t *s, const leader_cfg_t *cfg,
                               const leader_report_t *report, uint64_t now_us);
