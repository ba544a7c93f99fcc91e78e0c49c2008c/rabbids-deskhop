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

/* Read one string descriptor by index into an identity field. Empty on failure. */
static void read_string_field(uint8_t dev_addr,
                              uint8_t (*getter)(uint8_t, uint16_t, void *, uint16_t),
                              char *dst) {
    uint16_t temp[64];
    memset(temp, 0, sizeof(temp));
    dst[0] = '\0';

    uint8_t xfer_result = getter(dev_addr, IDENTITY_LANGID, temp, sizeof(temp));
    if (xfer_result == XFER_RESULT_SUCCESS && (temp[0] & 0xFF) >= 2)
        utf16_desc_to_ascii(temp, dst);
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

    state->present_identity        = *id;
    state->config.stored_identity  = *id;

    save_config(state);

    state->identity_reenum_pending = true;
}

void identity_capture_task(device_t *state) {
    uint8_t dev_addr = state->identity_capture_addr;
    if (dev_addr == 0)
        return;

    usb_identity_t id;
    memset(&id, 0, sizeof(id));

    uint16_t vid = 0, pid = 0;
    if (!tuh_vid_pid_get(dev_addr, &vid, &pid)) {
        /* Device went away before we could read it; drop the request. */
        state->identity_capture_addr = 0;
        return;
    }

    id.vid = vid;
    id.pid = pid;
    id.bcd = 0x0100;

    /* bcdDevice lives in the device descriptor; best-effort read. */
    tusb_desc_device_t desc;
    if (tuh_descriptor_get_device_sync(dev_addr, &desc, sizeof(desc)) == XFER_RESULT_SUCCESS)
        id.bcd = desc.bcdDevice;

    read_string_field(dev_addr, tuh_descriptor_get_manufacturer_string_sync, id.manufacturer);
    read_string_field(dev_addr, tuh_descriptor_get_product_string_sync, id.product);
    read_string_field(dev_addr, tuh_descriptor_get_serial_string_sync, id.serial);

    id.magic = USB_IDENTITY_MAGIC;
    id.valid = 1;

    apply_identity(state, &id);

    /* Share it with the other board so both outputs match. */
    state->identity_push_offset  = 0;
    state->identity_push_pending = true;

    state->identity_capture_addr = 0;
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
