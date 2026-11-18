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

#include "leader.h"

static bool lr_key_in(const leader_report_t *r, uint8_t key) {
    if (key == 0)
        return false;
    for (int i = 0; i < LEADER_KEYS; i++)
        if (r->keycode[i] == key)
            return true;
    return false;
}

static bool lr_empty(const leader_report_t *r) {
    if (r->modifier)
        return false;
    for (int i = 0; i < LEADER_KEYS; i++)
        if (r->keycode[i])
            return false;
    return true;
}

/* True if the only thing held is the leader key (no other keys, no modifiers). */
static bool lr_leader_only(const leader_report_t *r, uint8_t leader_key) {
    if (r->modifier)
        return false;

    bool seen = false;
    for (int i = 0; i < LEADER_KEYS; i++) {
        uint8_t k = r->keycode[i];
        if (k == 0)
            continue;
        if (k == leader_key)
            seen = true;
        else
            return false; /* some other key is held */
    }
    return seen;
}

leader_result_t leader_process(leader_state_t *s, const leader_cfg_t *cfg,
                               const leader_report_t *report, uint64_t now_us) {
    const bool leader_dn = lr_key_in(report, cfg->leader_key);
    const bool rising    = leader_dn && !s->prev_down;
    s->prev_down = leader_dn;

    /* Fast path: the vast majority of reports are normal typing with no sequence
       in progress. Do the minimum work and return before any timers/selectors. */
    if (s->state == LEADER_IDLE && !rising)
        return LEADER_PASS;

    const bool empty = lr_empty(report);

    switch (s->state) {
        case LEADER_SWALLOW:
            /* Keep eating the still-held selector/leader keys until full release. */
            if (empty)
                s->state = LEADER_IDLE;
            return empty ? LEADER_PASS : LEADER_CONSUME;

        case LEADER_ARMED: {
            if (now_us - s->time_us > (uint64_t)cfg->timeout_ms * 1000) {
                s->state = LEADER_IDLE;
                break; /* window expired: fall through to normal handling */
            }
            /* Look up the selector only now that we are actually armed. */
            leader_result_t sel = LEADER_PASS;
            if (lr_key_in(report, cfg->select_a))
                sel = LEADER_SWITCH_A;
            else if (lr_key_in(report, cfg->select_b))
                sel = LEADER_SWITCH_B;
            else if (lr_key_in(report, cfg->toggle))
                sel = LEADER_SWITCH_TOGGLE;

            if (sel != LEADER_PASS) {
                s->state = LEADER_SWALLOW;
                return sel;
            }
            /* Still holding/releasing the leader, or nothing yet: keep waiting. */
            if (leader_dn || empty)
                return LEADER_CONSUME;
            /* A different key: sequence abandoned, let it through (leader dropped). */
            s->state = LEADER_IDLE;
            break;
        }

        case LEADER_FIRST_TAP:
            if (now_us - s->time_us > (uint64_t)cfg->tap_window_ms * 1000) {
                s->state = LEADER_IDLE;
                break; /* too slow for a double-tap: treat as a fresh report */
            }
            if (rising) { /* second tap within the window -> armed */
                s->state   = LEADER_ARMED;
                s->time_us = now_us;
                return LEADER_CONSUME;
            }
            if (leader_dn || empty)
                return LEADER_CONSUME; /* holding/releasing the first tap: keep waiting */
            s->state = LEADER_IDLE;
            break;

        case LEADER_IDLE:
        default:
            break;
    }

    /* Arm on a fresh leader tap (its own key, nothing else held). */
    if (s->state == LEADER_IDLE && rising && lr_leader_only(report, cfg->leader_key)) {
        s->state   = cfg->double_tap ? LEADER_FIRST_TAP : LEADER_ARMED;
        s->time_us = now_us;
        return LEADER_CONSUME;
    }

    return LEADER_PASS;
}
