# iDotMatrix: making the panel beep (alarm + buzzer)

The iDotMatrix panels have a buzzer, but there is no plain "beep" command. The only way I found to sound it
from your own code is the panel's **alarm clock**: set a one-off alarm with the buzzer flag on, and the panel
beeps when it goes off.

This is how [`pixel_weather`](../components/pixel_weather) sounds a lightning warning.

**Tested on:** one 16x16 panel (advertises as `IDM-xxxxxx`), official Android app 2.1.7, November 2026.
Please report results for other sizes and firmware.

## How this was worked out
- The packet layout comes from reading the official Android app (2.1.7), which builds it in `TimerAgreement.sendData`.
- A Bluetooth HCI snoop capture of the app setting an alarm confirmed it byte for byte.
- It was then reproduced from an ESP32 running ESPHome. The panel beeped.

## GATT
| | UUID |
|---|---|
| Service | `000000fa-0000-1000-8000-00805f9b34fb` |
| Write | `0000fa02-0000-1000-8000-00805f9b34fb` |
| Notify | `0000fa03-0000-1000-8000-00805f9b34fb` |

Multi-byte numbers are little-endian unless stated otherwise.

## Step 1: set the panel's clock (required)

The panel has no battery-backed clock. Its time is whatever was last sent, and it is lost on power-off.
**Send the real local time first**, the same way the app does every time it connects:

```
0B 00 01 80 YY MM DD WD HH MI SS
```

| Byte | Meaning |
|---|---|
| `YY` | year − 2000 |
| `MM` | month 1–12 |
| `DD` | day 1–31 |
| `WD` | weekday: 1 = Monday … 7 = Sunday |
| `HH MI SS` | hour, minute, second |

The panel replies with a notify: `09 00 01 80 03 02 01 01 00`. The meaning of those bytes isn't known.

## Step 2: send the alarm

The alarm is a 24-byte header followed by the picture to show while it rings. A picture is required.

Pictures larger than 4096 bytes are split into 4096-byte parts. **Every part repeats the 24-byte header**;
the size and CRC always describe the whole picture.

| Offset | Size | Value |
|---|---|---|
| 0 | 2 | packet length (header + this part), LE |
| 2 | 2 | `00 80` |
| 4 | 1 | slot (see the slot note below) |
| 5 | 1 | flags: bit 0 = enabled, bits 1–7 = Monday…Sunday. `01` = once |
| 6 | 1 | hour |
| 7 | 1 | minute |
| 8 | 2 | how long it rings, in seconds, LE. The app offers 10, 30, 60, 300 and 900 |
| 10 | 1 | picture type: `01` = GIF file, `02` = raw 16x16 RGB (768 bytes), `03` = text |
| 11 | 1 | buzzer: `01` = on, `00` = off |
| 12 | 1 | `00` on the first part, `02` on the following parts |
| 13 | 4 | total picture size, LE |
| 17 | 4 | CRC-32 of the whole picture (standard zlib CRC-32), LE |
| 21 | 2 | `00 00` |
| 23 | 1 | picture id: the app sends **slot + 20** |
| 24… | | this part of the picture |

The panel acknowledges each part with `05 00 00 80 03`.

The app's own capture (slot 0, once, 21:18, 10 s, GIF, buzzer on, 4183-byte GIF):

```
18 10 00 80 00 01 15 12 0A 00 01 01 00 57 10 00 00 41 8C EA 21 00 00 14 47 49 46 38 39 61 …
```

To switch an alarm off, the app sends the first 12 bytes only: length `0C 00`, then `00 80`, slot, the flags
with bit 0 cleared, hour, minute, duration, type and buzzer. This comes from the app code and hasn't been tested.

## Things that tripped me up

- **Slot 0 beeps; slot 9 did not.** I used slot 9 to keep clear of alarms made in the app. The panel
  acknowledged it (`05 00 00 80 03`) but it never went off. The same packet in slot 0 beeped. The app allows
  10 alarms (0–9), so this may be specific to 16x16 panels. Slots 1–8 haven't been tested.
- **Don't fake the clock.** Setting the clock to a made-up time a few seconds before the alarm minute,
  *after* sending the alarm, never fired. Send the real time first, then the alarm for a later minute. The app
  itself sets alarms as little as about 5 seconds ahead.
- **The picture shows straight away.** The panel displays the alarm picture as a preview when it arrives, and
  again at the alarm time. That makes it easy to mistake the preview for the alarm going off.
- **The panel is busy after the alarm.** For a while after the alarm it keeps showing the alarm picture and
  ignores a new GIF upload. It acknowledges the upload but doesn't display it. An upload about 1 minute
  after the alarm didn't take; one about 3 minutes after did.
- **The phone app blocks other clients.** While the iDotMatrix app is connected, other clients get
  connect status 133.

## Other commands used by the component (for reference)

| What | Bytes |
|---|---|
| screen on / off | `05 00 07 01 01` / `05 00 07 01 00` |
| brightness 5–100 % | `05 00 04 80 <pct>` |
| GIF upload | 4096-byte parts, each with a 16-byte header: `<len LE> 01 00 <00 first / 02 rest> <total size LE32> <crc32 LE32> 05 00 0D`. The panel replies `05 00 01 00 03` once the whole GIF is stored. The replies to the parts in between haven't been checked. Wait about 3 s before disconnecting, or the panel may drop the upload. |

## Credits
- The [python3-idotmatrix-client](https://github.com/derkalle4/python3-idotmatrix-client) /
  [library](https://github.com/derkalle4/python3-idotmatrix-library) projects for the base protocol;
  issue #18 there asks about the alarm and buzzer.
- [idotmatrix-api-client](https://github.com/markusressel/idotmatrix-api-client) for its protocol notes.
- [IDotMatrix-ESP32-Emulator](https://github.com/piggei/IDotMatrix-ESP32-Emulator) for the first description of
  the alarm packet.
