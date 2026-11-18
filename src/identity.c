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

#include "main.h"
#include "tusb.h"

#define IDENTITY_LANGID    0x0409
#define IDENTITY_SER_LEN   ((uint16_t)sizeof(usb_identity_t))

/* Convert a USB UTF-16LE string descriptor to a NUL-terminated ASCII string.
   Non-ASCII code points are replaced by '?'. dst must hold USB_IDENTITY_STR_LEN bytes. */
static void utf16_desc_to_ascii(const uint16_t *desc, char *dst) {
    /* desc[0] low byte = bLength (header + payload) in bytes; descriptor type is desc[0] high byte */
    uint8_t total_len = (uint8_t)(desc[0] & 0xFF);
    uint8_t char_count = (total_len >= 2) ? (uint8_t)((total_len - 2) / 2) : 0;

    if (char_count > USB_IDENTITY_STR_LEN - 1)
        char_count = USB_IDENTITY_STR_LEN - 1;

    uint8_t out = 0;
    for (uint8_t i = 0; i < char_count; i++) {
        uint16_t ch = desc[1 + i];
        /* Keep printable ASCII only; the device-side string descriptor is ASCII anyway */
        dst[out++] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    dst[out] = '\0';
}

void identity_init(device_t *state) {
    if (state->config.clone_usb_identity && state->config.stored_identity.valid
        && state->config.stored_identity.magic == USB_IDENTITY_MAGIC) {
        state->present_identity = state->config.stored_identity;
    } else {
        memset(&state->present_identity, 0, sizeof(state->present_identity));
    }
}

const usb_identity_t *identity_to_present(device_t *state) {
    if (state->config_mode_active || !state->config.clone_usb_identity)
        return NULL;

    if (state->present_identity.valid && state->present_identity.magic == USB_IDENTITY_MAGIC)
        return &state->present_identity;

    return NULL;
}

void identity_request_capture(device_t *state, uint8_t dev_addr) {
    if (!state->config.clone_usb_identity || state->config_mode_active)
        return;

    /* Only one capture in flight; first keyboard wins until it is processed. */
    if (state->identity_capture_addr == 0)
        state->identity_capture_addr = dev_addr;
}

/* Store a freshly built identity as the one to present, persist it, and schedule
   a re-enumeration so the local host re-reads the descriptors. */
static void apply_identity(device_t *state, const usb_identity_t *id) {
    /* Ignore no-op updates to avoid needless re-enumeration / flash wear. */
    if (memcmp(&state->present_identity, id, sizeof(*id)) == 0)
        return;

    /* Publish safely for the other core, which reads present_identity in the USB
       descriptor callbacks: invalidate, fill, then validate, with barriers, so a
       concurrent GET_DESCRIPTOR never sees a half-written record (it falls back
       to the static identity during the brief window instead). */
    state->present_identity.valid = 0;
    __sync_synchronize();

    usb_identity_t tmp = *id;
    tmp.valid = 0;
    state->present_identity = tmp;
    __sync_synchronize();
    state->present_identity.valid = 1;
    __sync_synchronize();

    state->config.stored_identity = *id;
    save_config(state);

    state->identity_reenum_pending = true;
}

/* --- Asynchronous identity capture ------------------------------------------
 * The descriptor reads are done with the non-blocking TinyUSB API and advanced
 * from completion callbacks, so core1's loop is never stalled (a device that
 * stops responding can no longer trip the watchdog). Flash is written only from
 * the task context (capture_finish), never from a USB callback. A single capture
 * runs at a time, driven on core1, so module-static state is safe. */
enum { CAP_IDLE = 0, CAP_DEVICE, CAP_MFR, CAP_PROD, CAP_SER, CAP_DONE };

#define CAPTURE_TIMEOUT_US 1000000u /* abort a capture stuck mid-transfer after 1 s */

static volatile uint8_t cap_phase = CAP_IDLE;
static uint8_t  cap_addr;
static uint64_t cap_start_us;
static usb_identity_t cap_id;
static uint16_t cap_buf[64]; /* holds the device descriptor or one string descriptor */

static void capture_cb(tuh_xfer_t *xfer);

/* Kick the async read for a phase; on failure, hand off to the task to finalize. */
static void capture_kick(uint8_t phase) {
    cap_phase = phase;
    bool started = false;

    switch (phase) {
        case CAP_MFR:
            started = tuh_descriptor_get_manufacturer_string(cap_addr, IDENTITY_LANGID,
                                                             cap_buf, sizeof(cap_buf), capture_cb, 0);
            break;
        case CAP_PROD:
            started = tuh_descriptor_get_product_string(cap_addr, IDENTITY_LANGID,
                                                        cap_buf, sizeof(cap_buf), capture_cb, 0);
            break;
        case CAP_SER:
            started = tuh_descriptor_get_serial_string(cap_addr, IDENTITY_LANGID,
                                                       cap_buf, sizeof(cap_buf), capture_cb, 0);
            break;
    }

    if (!started)
        cap_phase = CAP_DONE; /* finalize with whatever we have (task context) */
}

static void capture_cb(tuh_xfer_t *xfer) {
    bool ok = (xfer->result == XFER_RESULT_SUCCESS);

    switch (cap_phase) {
        case CAP_DEVICE:
            if (ok)
                cap_id.bcd = ((const tusb_desc_device_t *)cap_buf)->bcdDevice;
            capture_kick(CAP_MFR);
            break;
        case CAP_MFR:
            if (ok && (cap_buf[0] & 0xFF) >= 2)
                utf16_desc_to_ascii(cap_buf, cap_id.manufacturer);
            capture_kick(CAP_PROD);
            break;
        case CAP_PROD:
            if (ok && (cap_buf[0] & 0xFF) >= 2)
                utf16_desc_to_ascii(cap_buf, cap_id.product);
            capture_kick(CAP_SER);
            break;
        case CAP_SER:
            if (ok && (cap_buf[0] & 0xFF) >= 2)
                utf16_desc_to_ascii(cap_buf, cap_id.serial);
            cap_phase = CAP_DONE; /* let the task apply + persist off the USB callback */
            break;
        default:
            break;
    }
}

static void capture_finish(device_t *state) {
    cap_id.magic = USB_IDENTITY_MAGIC;
    cap_id.valid = 1;

    apply_identity(state, &cap_id);

    /* Share it with the other board so both outputs match. */
    state->identity_push_offset  = 0;
    state->identity_push_pending = true;

    state->identity_capture_addr = 0;
    cap_phase = CAP_IDLE;
}

void identity_capture_task(device_t *state) {
    if (state->identity_capture_addr == 0)
        return;

    /* A capture finished (in a callback): apply it here, in task context. */
    if (cap_phase == CAP_DONE) {
        capture_finish(state);
        return;
    }

    /* A capture is already in flight (async): wait for it, but abort if a
       transfer never completes (e.g. the device vanished mid-capture) so the
       feature can't get stuck. A late callback then hits the default case. */
    if (cap_phase != CAP_IDLE) {
        if (time_us_64() - cap_start_us > CAPTURE_TIMEOUT_US) {
            cap_phase = CAP_IDLE;
            state->identity_capture_addr = 0;
        }
        return;
    }

    /* Start a new capture: VID/PID are available without a transfer. */
    uint16_t vid = 0, pid = 0;
    if (!tuh_vid_pid_get(state->identity_capture_addr, &vid, &pid)) {
        state->identity_capture_addr = 0;
        return;
    }

    memset(&cap_id, 0, sizeof(cap_id));
    cap_id.vid = vid;
    cap_id.pid = pid;
    cap_id.bcd = 0x0100;
    cap_addr     = state->identity_capture_addr;
    cap_start_us = time_us_64();

    cap_phase = CAP_DEVICE;
    if (!tuh_descriptor_get_device(cap_addr, cap_buf, sizeof(tusb_desc_device_t), capture_cb, 0))
        cap_phase = CAP_DONE; /* couldn't start; finalize with VID/PID next tick */
}

void identity_send_task(device_t *state) {
    if (!state->identity_push_pending)
        return;

    if (queue_is_full(&state->uart_tx_queue))
        return;

    const uint8_t *raw = (const uint8_t *)&state->present_identity;
    uint16_t offset    = state->identity_push_offset;

    uint8_t len = IDENTITY_CHUNK_BYTES;
    if (offset + len > IDENTITY_SER_LEN)
        len = (uint8_t)(IDENTITY_SER_LEN - offset);

    uint8_t data[PACKET_DATA_LENGTH] = {0};
    data[0] = (uint8_t)offset;
    data[1] = len;
    memcpy(&data[2], raw + offset, len);
    queue_packet(data, IDENTITY_SYNC_MSG, PACKET_DATA_LENGTH);

    state->identity_push_offset += len;
    if (state->identity_push_offset >= IDENTITY_SER_LEN)
        state->identity_push_pending = false;
}

void handle_identity_sync_msg(uart_packet_t *packet, device_t *state) {
    if (!state->config.clone_usb_identity)
        return;

    uint8_t offset = packet->data[0];
    uint8_t len    = packet->data[1];

    /* Strict bounds: reject anything that would write outside the buffer. */
    if (len == 0 || len > IDENTITY_CHUNK_BYTES || offset >= IDENTITY_SER_LEN
        || (uint16_t)offset + len > IDENTITY_SER_LEN)
        return;

    /* Chunks are expected in order; a gap or restart resets the reassembly. */
    if (offset == 0)
        state->identity_rx_len = 0;
    if (offset != state->identity_rx_len)
        return;

    memcpy(&state->identity_rx_buf[offset], &packet->data[2], len);
    state->identity_rx_len = offset + len;

    if (state->identity_rx_len < IDENTITY_SER_LEN)
        return;

    /* Full record received: validate before applying. */
    usb_identity_t incoming;
    memcpy(&incoming, state->identity_rx_buf, sizeof(incoming));
    state->identity_rx_len = 0;

    if (incoming.magic != USB_IDENTITY_MAGIC || !incoming.valid)
        return;

    /* Guarantee NUL termination regardless of what arrived over the wire. */
    incoming.manufacturer[USB_IDENTITY_STR_LEN - 1] = '\0';
    incoming.product[USB_IDENTITY_STR_LEN - 1]      = '\0';
    incoming.serial[USB_IDENTITY_STR_LEN - 1]       = '\0';

    apply_identity(state, &incoming);
}

void identity_reenumerate_task(device_t *state) {
    /* Simple two-phase state machine driven by the task scheduler. phase lives
       across calls; timing uses the microsecond clock so we never block. */
    static uint8_t phase = 0;
    static uint64_t deadline = 0;

    switch (phase) {
        case 0:
            if (!state->identity_reenum_pending)
                return;
            /* Only bounce the link once the host has actually enumerated us. */
            if (!tud_mounted()) {
                state->identity_reenum_pending = false;
                return;
            }
            tud_disconnect();
            deadline = time_us_64() + 120000; /* hold disconnect ~120 ms */
            phase = 1;
            break;

        case 1:
            if (time_us_64() < deadline)
                return;
            tud_connect();
            state->identity_reenum_pending = false;
            phase = 0;
            break;
    }
}
