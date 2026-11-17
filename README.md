
# Device support request: HyperX Pulsefire Haste 2 Wireless

Find the exe in release.

**Device information:**
- Device: HyperX Pulsefire Haste 2 Wireless
- VID: `03F0`
- PID: `0F98`
- Interface: `MI_02`
- Connection: 2.4 GHz wireless receiver

**Reverse-engineered battery protocol:**

I successfully queried the battery level on Windows using HID communication.

- Output report length: 64 bytes
- Input report length: 64 bytes
- Request: `50 02` (64-byte report)
- Response header: `51 02`
- Battery percentage: byte index 2 (zero-based)

The protocol was tested on real hardware using a standalone C++ application. Battery percentage was retrieved successfully.

## Reverse-engineering documentation

The HID battery-query investigation and protocol reference are documented here:

- [Reverse-engineering notes](docs/REVERSE_ENGINEERING.md)
- [HID battery protocol reference](docs/HID_PROTOCOL.md)

The documented protocol was tested on a physical HyperX Pulsefire Haste 2 Wireless using its 2.4 GHz receiver. The battery percentage returned by the application matched HyperX NGENUITY during testing.

---
# Credits

[Wireless Device Battery Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/tree/main) : krisgfx9

