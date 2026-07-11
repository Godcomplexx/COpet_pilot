# BLE advertising learning log

Date: 2026-07-02

## What we are trying to prove

The ESP32 can advertise as a BLE peripheral, accept one phone connection, and
stop Bluetooth activity when Phone Bridge mode is closed.

## Interface

Bluetooth Low Energy using the ESP-IDF NimBLE host. No external wiring is
required.

## Smallest test

1. Initialize NimBLE without advertising.
2. Enter `PHONE BRIDGE`.
3. Advertise the complete name `CoPet Pilot`.
4. Connect with a phone BLE scanner.
5. Display `CONNECTED`.
6. Hold touch to leave the mode, disconnect, and stop advertising.

The connection lifecycle from this lesson is now used by the separate FFF1
message-write lesson.

## Verification

Expected serial sequence:

```text
Phone Bridge initialized; advertising is off
NimBLE host synchronized
Advertising as CoPet Pilot
Phone connected
Phone Bridge stopped
```

Expected screen states:

```text
ADVERTISING
CONNECTED
```

## Common failures

- Device absent from normal Bluetooth settings: use a BLE scanner.
- `BLE ERROR`: inspect the NimBLE return code in the serial log.
- Reboot before the menu with `ESP_ERR_NO_MEM`: NimBLE reduces contiguous
  internal RAM. Keep the compact RGB332 framebuffer and RGB565 DMA stripe.
- Device keeps advertising after leaving: verify `copet_ble_stop()` is called
  during the state transition back to the menu.

## What was learned

Advertising, connecting, and exchanging application data are separate BLE
steps. Phone Bridge controls when the radio is discoverable.
