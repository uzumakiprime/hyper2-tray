
# Device support request: HyperX Pulsefire Haste 2 Wireless

I would like to contribute support for the HyperX Pulsefire Haste 2 Wireless mouse.

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

I noticed that HyperX headsets are already supported in `src/drivers/hyperx.cpp`, while HyperX Pulsefire mice are currently listed as unsupported.

I believe this device could be integrated into the existing HyperX driver as a separate mouse model.

I can provide the HID diagnostic output and help test the implementation.

Would you be interested in accepting a contribution for this device?

