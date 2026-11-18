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

#include <stdbool.h>
#include <stdint.h>
#include "structs.h"

/* Number of identity payload bytes carried by a single UART packet.
   Layout of IDENTITY_SYNC_MSG data[8]: [0]=offset, [1]=length, [2..7]=payload. */
#define IDENTITY_CHUNK_BYTES 6

/* Restore a persisted identity into the live state at boot (call after load_config). */
void identity_init(device_t *state);

/* Request (from the USB host callback) that the device on dev_addr be cloned.
   Non-blocking: the actual descriptor reads happen in identity_capture_task. */
void identity_request_capture(device_t *state, uint8_t dev_addr);

/* core1: perform the (blocking) descriptor reads for a pending capture. */
void identity_capture_task(device_t *state);

/* core1: push a captured identity to the other board, one UART chunk per call. */
void identity_send_task(device_t *state);

/* core0: re-enumerate the device port so the host re-reads the descriptors. */
void identity_reenumerate_task(device_t *state);

/* UART handler: reassemble an identity sent by the other board. */
void handle_identity_sync_msg(uart_packet_t *packet, device_t *state);

/* Returns the identity the device descriptors should present, or NULL to use
   the static (compile-time) identity. Never returns a value in config mode. */
const usb_identity_t *identity_to_present(device_t *state);
