# BLE message write learning log

Date: 2026-07-02

## What we are trying to prove

A phone can write a short application message over BLE and CoPet can display
it without drawing from the NimBLE callback.

## GATT interface

| Item | UUID | Access |
|---|---|---|
| CoPet Bridge service | `FFF0` | service |
| Incoming message | `FFF1` | write / write without response |

The payload is 1–64 bytes of ASCII or simple UTF-8 text. Lowercase English is
converted to uppercase because the current display font is uppercase-only.

## Data flow

```text
phone writes FFF1
→ NimBLE callback validates and queues the message
→ main application loop receives the queue item
→ Phone Bridge screen redraws
```

## Verification

1. Open `PHONE BRIDGE`.
2. Connect to `CoPet Pilot` with BLE Connect.
3. Open service `FFF0`, characteristic `FFF1`.
4. Choose text/UTF-8 write and send `HELLO COPET`.
5. Confirm the screen and serial log show `HELLO COPET`.

Expected log:

```text
RX FFF1: HELLO COPET
Phone message displayed: HELLO COPET
```

## Common failures

- Writing to `FFF0`: it is a service, not a characteristic.
- Hex mode: text appears as unrelated characters; select text/UTF-8 mode.
- Payload over 64 bytes: BLE returns invalid attribute length.

## What was learned

BLE callbacks and display rendering run in different task contexts. A queue
provides a clear, thread-safe boundary between transport and UI.
