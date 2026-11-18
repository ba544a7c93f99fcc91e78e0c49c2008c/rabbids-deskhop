## Example USB identity profile. Copy it, edit the values, then build with:
##   cmake -C misc/usb_identity/my_profile.cmake -S . -B build && cmake --build build
## Every value is optional; anything left out keeps the stock DeskHop default.
## Strings: printable ASCII, max 31 chars, no " or \.

# Board A (the one your keyboard plugs into by default)
set(DH_USB_A_VID          "0x1209"           CACHE STRING "")
set(DH_USB_A_PID          "0xC000"           CACHE STRING "")
set(DH_USB_A_BCD          "0x0100"           CACHE STRING "")
set(DH_USB_A_MANUFACTURER "My Company"       CACHE STRING "")
set(DH_USB_A_PRODUCT      "Desk Switch A"    CACHE STRING "")
set(DH_USB_A_SERIAL       ""                 CACHE STRING "")  # empty = chip unique ID
set(DH_USB_A_HELPER       "Desk Helper A"    CACHE STRING "")

# Board B
set(DH_USB_B_VID          "0x1209"           CACHE STRING "")
set(DH_USB_B_PID          "0xC000"           CACHE STRING "")
set(DH_USB_B_BCD          "0x0100"           CACHE STRING "")
set(DH_USB_B_MANUFACTURER "My Company"       CACHE STRING "")
set(DH_USB_B_PRODUCT      "Desk Switch B"    CACHE STRING "")
set(DH_USB_B_SERIAL       ""                 CACHE STRING "")
set(DH_USB_B_HELPER       "Desk Helper B"    CACHE STRING "")
