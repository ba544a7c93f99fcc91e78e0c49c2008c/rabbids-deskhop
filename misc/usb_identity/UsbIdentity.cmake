## USB identity announced by each board (A and B) in normal mode.
## Override with -D on the command line, or load a profile with:
##   cmake -C misc/usb_identity/example.cmake -S . -B build
## Config mode (USB disk / web config) always keeps its stock identity.

foreach(board A B)
  set(DH_USB_${board}_VID          "0x1209"          CACHE STRING "Board ${board}: USB Vendor ID (hex, e.g. 0x1209)")
  set(DH_USB_${board}_PID          "0xC000"          CACHE STRING "Board ${board}: USB Product ID (hex)")
  set(DH_USB_${board}_BCD          "0x0100"          CACHE STRING "Board ${board}: device release number bcdDevice (hex)")
  set(DH_USB_${board}_MANUFACTURER "Hrvoje Cavrak"   CACHE STRING "Board ${board}: manufacturer string")
  set(DH_USB_${board}_PRODUCT      "DeskHop Switch"  CACHE STRING "Board ${board}: product string")
  set(DH_USB_${board}_SERIAL       ""                CACHE STRING "Board ${board}: serial string (empty = chip unique ID)")
  set(DH_USB_${board}_HELPER       "DeskHop Helper"  CACHE STRING "Board ${board}: relative-mouse interface name")

  foreach(field VID PID BCD)
    set(value "${DH_USB_${board}_${field}}")
    if(NOT value MATCHES "^0[xX][0-9a-fA-F]+$")
      message(FATAL_ERROR "DH_USB_${board}_${field}='${value}' must be a hex value like 0x1234")
    endif()
    math(EXPR numeric "${value}")
    if(numeric GREATER 65535)
      message(FATAL_ERROR "DH_USB_${board}_${field}='${value}' does not fit in 16 bits")
    endif()
  endforeach()

  foreach(field MANUFACTURER PRODUCT SERIAL HELPER)
    set(value "${DH_USB_${board}_${field}}")
    string(LENGTH "${value}" len)
    if(len GREATER 31)
      message(FATAL_ERROR "DH_USB_${board}_${field}='${value}' is ${len} chars, max is 31")
    endif()
    # Printable ASCII only, no quote or backslash (it ends up in a C string literal)
    string(FIND "${value}" "\"" has_quote)
    string(FIND "${value}" "\\" has_backslash)
    if(NOT value MATCHES "^[ -~]*$" OR has_quote GREATER -1 OR has_backslash GREATER -1)
      message(FATAL_ERROR "DH_USB_${board}_${field}='${value}' must be printable ASCII without quote or backslash")
    endif()
  endforeach()

  message(STATUS "USB identity board ${board}: ${DH_USB_${board}_VID}:${DH_USB_${board}_PID} "
                 "'${DH_USB_${board}_MANUFACTURER}' / '${DH_USB_${board}_PRODUCT}'")
endforeach()

set(DH_USB_IDENTITY_DIR ${CMAKE_BINARY_DIR}/generated)
configure_file(${CMAKE_CURRENT_LIST_DIR}/usb_identity_config.h.in ${DH_USB_IDENTITY_DIR}/usb_identity_config.h @ONLY)
