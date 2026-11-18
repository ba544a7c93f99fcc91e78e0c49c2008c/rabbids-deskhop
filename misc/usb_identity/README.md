# USB identity (VID / PID / strings) of the outputs

DeskHop can make the computers see a chosen USB identity instead of the stock
`DeskHop Switch`. There are two mechanisms, and they stack:

1. **Dynamic cloning (default)** — replicate the connected keyboard's identity
   onto *both* outputs, at runtime.
2. **Static per-board identity** — hard-code an identity for board A and B at
   build time (used as the fallback when nothing has been cloned).

Config mode (USB disk + web config) always keeps the stock identity
(`2e8a:107c`), because the web config page filters on it.

---

## 1. Dynamic cloning (default)

When `CLONE_USB_IDENTITY` is `1` (the default, in `src/include/user_config.h`):

- The board the **keyboard** is plugged into reads the keyboard's VID, PID,
  bcdDevice and manufacturer/product/serial strings when it connects.
- It shares them with the other board over the UART link, so **both outputs
  announce the same identity** — no variation between A and B.
- Each board re-enumerates (briefly disconnects/reconnects on its USB port) so
  the computer re-reads the descriptors.
- The identity is saved in flash, so it stays the same across reboots until a
  different keyboard is connected.

### Important limitation

A USB output port is **one** composite device and can carry **one** VID/PID.
So the outputs replicate the **keyboard** identity; the mouse keeps working but
its own VID/PID/strings are not replicated. The HID report descriptors (the data
format) remain DeskHop's.

To disable dynamic cloning and keep the static identity below, set
`#define CLONE_USB_IDENTITY 0` in `src/include/user_config.h` and rebuild.

> Cloning a vendor VID/PID can make the host load that vendor's driver/software,
> which may expect a different interface layout.

---

## 2. Static per-board identity (compile-time)

Each board can instead announce a fixed identity set at build time. The single
`.uf2` contains both; each board picks its own at boot. These values are also
the fallback used before any keyboard has been cloned.

| Variable                    | Default          | Notes                                |
|-----------------------------|------------------|--------------------------------------|
| `DH_USB_<A/B>_VID`          | `0x1209`         | hex, 16 bits                         |
| `DH_USB_<A/B>_PID`          | `0xC000`         | hex, 16 bits                         |
| `DH_USB_<A/B>_BCD`          | `0x0100`         | bcdDevice (device release)           |
| `DH_USB_<A/B>_MANUFACTURER` | `Hrvoje Cavrak`  | ASCII, max 31 chars, no `"` or `\`   |
| `DH_USB_<A/B>_PRODUCT`      | `DeskHop Switch` | same                                 |
| `DH_USB_<A/B>_SERIAL`       | *(empty)*        | empty = Pico unique ID               |
| `DH_USB_<A/B>_HELPER`       | `DeskHop Helper` | name of the relative-mouse interface |

### Clone a real device into a static profile

On Linux, with the device plugged in:

    misc/usb_identity/clone_usb_identity.py --list
    misc/usb_identity/clone_usb_identity.py --a <VID:PID> --b <VID:PID> -o misc/usb_identity/my_profile.cmake

### Build with a profile

    cmake -C misc/usb_identity/my_profile.cmake -S . -B build
    cmake --build build

Use a fresh build dir (or delete `build/CMakeCache.txt`) when switching profiles,
since cached values otherwise take precedence over a new `-C` file.

---

## Flashing

Flash `build/deskhop.uf2` as usual (config-mode drive, or hold BOOTSEL while
plugging in). Because the config layout changed, existing saved settings reset to
defaults on first boot after upgrading.
