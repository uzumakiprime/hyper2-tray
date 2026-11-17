# HyperX Pulsefire Haste 2 Wireless --- HID Battery Protocol Reference

A concise reference for the battery-query operation verified on a HyperX
Pulsefire Haste 2 Wireless connected through its 2.4 GHz USB receiver.

## Device identification

  Field                        Value
  ---------------------------- -----------------------------------
  Device                       HyperX Pulsefire Haste 2 Wireless
  VID                          `0x03F0`
  PID                          `0x0F98`
  Windows HID path interface   `MI_02`
  Diagnostic interface index   `2`
  Usage page                   `0xFF00`
  Usage                        `0x0001`
  Input report length          64 bytes
  Output report length         64 bytes
  Feature report length        64 bytes

## Request

Send one 64-byte output report:

    Byte index    Value
  ------------ --------
             0   `0x50`
             1   `0x02`
         2--63   `0x00`

``` text
50 02 00 00 00 00 ... (64 bytes total)
```

## Response

Accept an input report whose first two bytes are:

``` text
51 02
```

The battery percentage is at zero-based byte index `2`.

``` text
response[0] == 0x51
response[1] == 0x02
battery_percent = response[2]
```

The tested implementation accepts values from `0` through `100`.

### Example

``` text
51 02 1D ...
```

`0x1D` is decimal `29`, so the battery percentage is `29%`.

This is an illustrative prefix, not a full captured report.

## Implementation notes

-   Obtain report lengths from the HID collection's capabilities where
    possible.
-   Use the vendor-defined collection (`0xFF00:0x0001`), not the
    standard mouse or consumer-control collections.
-   Apply a finite timeout to the read operation.
-   Validate the response header before interpreting byte index `2`.
-   Do not infer meanings for the remaining response bytes without
    further evidence.
-   This reference documents the battery query only; it does not
    document configuration or control commands.

## Verification status

Tested on physical HyperX Pulsefire Haste 2 Wireless hardware using the
2.4 GHz receiver. The returned percentage matched HyperX NGENUITY during
testing.

See [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) for the
investigation notes, interface enumeration and scope limitations.
