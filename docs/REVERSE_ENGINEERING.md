# Reverse Engineering the HyperX Pulsefire Haste 2 Wireless Battery Protocol

This document records the investigation used to read the battery
percentage from a **HyperX Pulsefire Haste 2 Wireless** mouse over its
2.4 GHz USB receiver.

The goal was limited and specific: identify the HID collection that
exposes the battery query, send the battery request, validate the
response, and extract the percentage. This is not a complete reverse
engineering of HyperX NGENUITY or every command supported by the mouse.

## 1. Device and test setup

  -----------------------------------------------------------------------
  Property                            Observed value
  ----------------------------------- -----------------------------------
  Device                              HyperX Pulsefire Haste 2 Wireless

  Vendor ID (VID)                     `03F0`

  Product ID (PID)                    `0F98`

  Connection used for testing         2.4 GHz USB receiver

  Windows device-path interface       `MI_02`
  suffix used by the application      

  HID interface index shown by the    `2`
  diagnostic utility                  

  Usage page                          `0xFF00`

  Usage                               `0x0001`

  Input report length                 64 bytes

  Output report length                64 bytes

  Feature report length               64 bytes
  -----------------------------------------------------------------------

**Interface terminology:** `MI_02` is the interface identifier observed
in the Windows HID device path. The diagnostic utility lists the
corresponding collection as interface index `2`. These are two
representations of the same collection in the observed setup; do not
assume that interface numbering is identical across every operating
system or connection mode.

## 2. Enumerating the HID collections

The receiver exposes multiple HID collections. The diagnostic output
included these entries for VID/PID `03F0:0F98`:

    Interface index   Usage page      Usage   Input   Output   Feature
  ----------------- ------------ ---------- ------- -------- ---------
                  0     `0x0001`   `0x0002`      12        0         0
                  1     `0x0001`   `0x0006`       9        2         0
                  1     `0x000C`   `0x0001`       3        0         0
                  2     `0xFF00`   `0x0001`      64       64        64

The vendor-defined collection at interface index `2`, usage page
`0xFF00`, usage `0x0001`, was the relevant collection for the battery
protocol.

The Windows implementation enumerates HID device paths and selects the
path containing:

-   `vid_03f0&pid_0f98`
-   `mi_02`

It then obtains the HID report lengths from the device's preparsed data
rather than assuming that every collection has the same report size.

## 3. Identifying the battery request

The working implementation sends a 64-byte output report. The first two
bytes are:

``` text
50 02
```

The remaining bytes are zero-padded.

Conceptually, the request is:

``` text
50 02 00 00 00 00 ... (64 bytes total)
```

The application then reads an input report from the same HID collection.

## 4. Validating and interpreting the response

The response is accepted when its first two bytes match:

``` text
51 02
```

The battery percentage is stored at **zero-based byte index 2**.

    Byte index Meaning
  ------------ ----------------------------------------
             0 Response marker `0x51`
             1 Response command/type `0x02`
             2 Battery percentage
      3 onward Not interpreted by this implementation

The implementation accepts percentage values from `0` through `100`.

For example, if the response begins:

``` text
51 02 1D ...
```

then `0x1D` is decimal `29`, so the reported battery level is **29%**.

This example illustrates the byte interpretation; it is not presented as
a complete captured 64-byte response.

## 5. Implementation in the Windows application

The standalone application uses the Windows HID and SetupAPI interfaces
to:

1.  Enumerate present HID device interfaces.
2.  Match the VID/PID and `MI_02` interface path.
3.  Open the selected path for read/write access.
4.  Query `HIDP_CAPS` to obtain input and output report lengths.
5.  Send the 64-byte request beginning with `50 02`.
6.  Read the input report with a bounded wait.
7.  Validate the response header `51 02`.
8.  Read the percentage from `response[2]`.

The application polls periodically and displays the most recently read
percentage. The battery query does not require changing lighting, DPI,
profiles, or other mouse settings.

## 6. Validation

The protocol was tested with the physical HyperX Pulsefire Haste 2
Wireless and its 2.4 GHz receiver.

The percentage reported by the standalone application was compared with
the battery percentage shown by **HyperX NGENUITY**. During testing, the
two values matched.

The protocol was also adapted for the [Wireless Device Battery
Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor)
project and tested there. The upstream application displayed the mouse
and its battery percentage successfully.

## 7. Scope and limitations

The findings in this document are limited to the tested device,
receiver, interface and battery-query operation.

This work does **not** establish:

-   The complete NGENUITY communication protocol.
-   The meaning of all bytes in the 64-byte response.
-   Commands for DPI, polling rate, lighting, profiles or firmware
    operations.
-   Behaviour across every firmware version, receiver revision,
    operating system or wired/Bluetooth mode.
-   Whether the same protocol applies to other HyperX Pulsefire models.

Only the battery query described here has been implemented and
validated. Avoid sending undocumented commands to the device.

## 8. Reproduction checklist

To reproduce the observed battery query:

1.  Connect the Pulsefire Haste 2 Wireless 2.4 GHz receiver.
2.  Enumerate HID collections and identify VID `03F0`, PID `0F98`.
3.  Select the vendor-defined collection: interface `2`, usage page
    `0xFF00`, usage `0x0001` (Windows path suffix `MI_02` in the tested
    setup).
4.  Confirm that input and output reports are 64 bytes.
5.  Send a 64-byte output report beginning with `50 02`.
6.  Read the input report and verify the first two bytes are `51 02`.
7.  Interpret byte index `2` as the battery percentage.
8.  Compare the result with HyperX NGENUITY.



