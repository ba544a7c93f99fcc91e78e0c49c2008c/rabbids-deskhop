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

#include <stdint.h>
#include "flash.h"
#include "packet.h"
#include "screen.h"

typedef void (*action_handler_t)();

typedef struct { // Maps message type -> message handler function
    enum packet_type_e type;
    action_handler_t handler;
} uart_handler_t;

typedef struct {
    uint8_t modifier;                 // Which modifier is pressed
    uint8_t keys[KEYS_IN_USB_REPORT]; // Which keys need to be pressed
    uint8_t key_count;                // How many keys are pressed
    action_handler_t action_handler;  // What to execute when the key combination is detected
    bool pass_to_os;                  // True if we are to pass the key to the OS too
    bool acknowledge;                 // True if we are to notify the user about registering keypress
} hotkey_combo_t;

typedef struct TU_ATTR_PACKED {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t wheel;
    int8_t pan;
    uint8_t mode;
} mouse_report_t;

typedef struct {
    uint8_t tip_pressure;
    uint8_t buttons; // Digitizer buttons
    uint16_t x;      // X coordinate (0-32767)
    uint16_t y;      // Y coordinate (0-32767)
} touch_report_t;

typedef struct {
    uint8_t instance;
    uint8_t report_id;
    uint8_t type;
    uint8_t len;
    uint8_t data[RAW_PACKET_LENGTH];
} hid_generic_pkt_t;

typedef enum { IDLE, READING_PACKET, PROCESSING_PACKET } receiver_state_t;

typedef struct {
    uint32_t address;         // Address we're sending to the other box
    uint32_t checksum;
    uint16_t version;
    bool byte_done;           // Has the byte been successfully transferred
    bool upgrade_in_progress; // True if firmware transfer from the other box is in progress
} fw_upgrade_state_t;

/* USB identity (VID/PID/strings) captured from a connected device and replicated
   on the outputs. Also used as the on-wire serialization between the two boards,
   so it must stay packed and byte-for-byte identical on both. */
#define USB_IDENTITY_MAGIC   0xD5
#define USB_IDENTITY_STR_LEN 32   /* includes the NUL terminator; 31 usable chars */

typedef struct __attribute__((packed)) {
    uint8_t  magic;   // USB_IDENTITY_MAGIC when this record is meaningful
    uint8_t  valid;   // 1 once an identity has been captured
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd;
    char manufacturer[USB_IDENTITY_STR_LEN];
    char product[USB_IDENTITY_STR_LEN];
    char serial[USB_IDENTITY_STR_LEN];
} usb_identity_t;

typedef struct {
    uint32_t magic_header;
    uint32_t version;

    uint8_t force_mouse_boot_mode;
    uint8_t force_kbd_boot_protocol;

    uint8_t kbd_led_as_indicator;
    uint8_t hotkey_toggle;
    uint8_t enable_acceleration;

    uint8_t enforce_ports;
    uint16_t jump_threshold;

    output_t output[NUM_SCREENS];

    /* If not set to zero (previously reserved), gaming mode should be active by default */
    uint8_t gaming_mode_default;
    uint8_t led_mode;

    /* When set, replicate the connected keyboard's USB identity on both outputs. */
    uint8_t clone_usb_identity;

    /* When set, outputs switch only via the hotkey, never by the mouse hitting
       a screen border. */
    uint8_t switch_hotkey_only;

    /* Last captured identity, persisted so the outputs don't change across reboots. */
    usb_identity_t stored_identity;

    // Keep checksum at the end of the struct
    uint32_t checksum;
} config_t;


/*==============================================================================
 *  Device State
 *==============================================================================*/
typedef struct {
    uint8_t kbd_dev_addr; // Address of the Keyboard device
    uint8_t kbd_instance; // Keyboard instance (d'uh - isn't this a useless comment)

    uint8_t keyboard_leds_desired[NUM_SCREENS];  // Desired state of keyboard LEDs (index 0 = A, index 1 = B)
    uint8_t keyboard_leds_actual[NUM_SCREENS];   // Actual state of keyboard LEDs
    uint64_t last_activity[NUM_SCREENS]; // Timestamp of the last input activity (-||-)
    uint32_t core1_last_loop_pass;       // Timestamp of last core1 loop execution
    uint8_t active_output;               // Currently selected output (0 = A, 1 = B)
    uint8_t board_role;                  // Which board are we running on? (0 = A, 1 = B, etc.)

    hid_keyboard_report_t local_kbd_states[MAX_DEVICES]; // Store keyboard states
    hid_keyboard_report_t remote_kbd_state;              // Store combined remote keyboard state
    uint8_t max_kbd_idx;                                 // Store largest kbd_idx seen

    int16_t pointer_x; // Store and update the location of our mouse pointer
    int16_t pointer_y;
    int16_t mouse_buttons; // Store and update the state of mouse buttons

    config_t config;       // Device configuration, loaded from flash or defaults used
    queue_t hid_queue_out; // Queue that stores outgoing hid messages
    queue_t kbd_queue;     // Queue that stores keyboard reports
    queue_t mouse_queue;   // Queue that stores mouse reports
    queue_t uart_tx_queue; // Queue that stores outgoing packets

    hid_interface_t iface[MAX_DEVICES][MAX_INTERFACES]; // Store info about HID interfaces
    uart_packet_t in_packet;

    /* DMA */
    uint32_t dma_ptr;             // Stores info about DMA ring buffer last checked position
    uint32_t dma_rx_channel;      // DMA RX channel we're using to receive
    uint32_t dma_control_channel; // DMA channel that controls the RX transfer channel
    uint32_t dma_tx_channel;      // DMA TX channel we're using to send

    /* Firmware */
    fw_upgrade_state_t fw;           // State of the firmware upgrader
    firmware_metadata_t _running_fw; // RAM copy of running fw metadata
    bool reboot_requested;           // If set, stop updating watchdog
    uint64_t config_mode_timer;      // Counts how long are we to remain in config mode

    uint8_t page_buffer[FLASH_PAGE_SIZE]; // For firmware-over-serial upgrades

    /* Connection status flags */
    bool tud_connected;      // True when TinyUSB device successfully connects
    bool keyboard_connected; // True when our keyboard is connected locally
    bool mouse_connected;    // True when our mouse is connected locally

    /* Leader-key output switching (anti-phantom). Tap(s) of the leader key are
       withheld from the PC; a selector key then switches output. */
    uint8_t  leader_state;        // 0 idle, 1 first tap seen, 2 armed, 3 swallow until release
    uint64_t leader_time;         // Timestamp of the last leader state transition
    bool     leader_prev_down;    // Was the leader key held in the previous report (edge detect)

    /* USB identity cloning (replicate the keyboard's VID/PID/strings on the outputs) */
    usb_identity_t present_identity;        // Identity currently announced to the host
    volatile uint8_t identity_capture_addr; // Nonzero: core1 should capture this device's identity
    volatile bool identity_push_pending;    // core1 should (re)transmit identity to the other board
    volatile uint16_t identity_push_offset; // Progress of the chunked UART transmission
    volatile bool identity_reenum_pending;  // core0 should re-enumerate the device port
    uint8_t  identity_rx_buf[sizeof(usb_identity_t)]; // Reassembly buffer for incoming identity
    uint16_t identity_rx_len;               // Bytes received so far into identity_rx_buf

    /* Feature flags */
    bool mouse_zoom;         // True when "mouse zoom" is enabled
    bool switch_lock;        // True when device is prevented from switching
    bool onboard_led_state;  // True when LED is ON
    bool relative_mouse;     // True when relative mouse mode is used
    bool gaming_mode;        // True when gaming mode is on (relative passthru + lock)
    bool config_mode_active; // True when config mode is active
    bool digitizer_active;   // True when digitizer Win/Mac workaround is active

    /* Onboard LED blinky (provide feedback when e.g. mouse connected) */
    int32_t  blinks_left;     // How many blink transitions are left
    uint32_t last_led_change; // Timestamp of the last time led state transitioned
} device_t;
/*==============================================================================*/


typedef struct {
    void (*exec)(device_t *state);
    uint64_t frequency;
    uint64_t next_run;
    bool *enabled;
} task_t;

enum os_type_e {
    LINUX   = 1,
    MACOS   = 2,
    WINDOWS = 3,
    ANDROID = 4,
    OTHER   = 255,
};

enum screen_pos_e {
    NONE   = 0,
    LEFT   = 1,
    RIGHT  = 2,
    MIDDLE = 3,
};

enum screensaver_mode_e {
    DISABLED   = 0,
    PONG       = 1,
    JITTER     = 2,
    MAX_SS_VAL = JITTER,
};

enum led_mode_e {
    LED_ACTIVE_OUTPUT = 0,
    LED_SHORT_IDLE    = 1,
    LED_LONG_IDLE     = 2,
    LED_STATUS_ONLY   = 3,
    LED_DISABLED      = 4,
};

extern const config_t default_config;
extern const config_t ADDR_CONFIG[];
extern const uint8_t ADDR_FW_METADATA[];
extern const uint8_t ADDR_FW_RUNNING[];
extern const uint8_t ADDR_FW_STAGING[];
extern const uint8_t ADDR_DISK_IMAGE[];
