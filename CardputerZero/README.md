# CardputerZero

A **Flipper Zero–style multi-tool firmware** for the **M5Stack Cardputer Adv**
with the **Cap CC1101 & NFC** expansion, plus the M5Stack IR unit and (optional)
433 MHz transmitter. Written from scratch in C++/Arduino on top of M5Stack's own
official libraries.

> ⚠️ **Use it on your own things.** CardputerZero is for learning radio/NFC/IR
> and automating devices, remotes, tags and computers **you own or are
> explicitly authorised to test**. Read [`docs/SAFETY_AND_LEGAL.md`](docs/SAFETY_AND_LEGAL.md)
> before you transmit anything.

---

## What it does

| Tool | Hardware used | What it does |
|------|---------------|--------------|
| **Sub-GHz** | CC1101 (on the Cap) | Receive & replay raw OOK bursts (315 / 433 / 868–915 MHz), a live RSSI frequency analyzer, save/load captures to SD |
| **NFC / RFID** | ST25R3916 (on the Cap) | Read & identify 13.56 MHz NFC-A tags (UID / type / ATQA / SAK), log identities to SD, full memory dump over USB serial. Covers your NTAG203 and MIFARE Classic 1K |
| **Infrared** | Built-in LED + Mini IR Unit | Learn a button from a real remote, replay it, universal TV-power sweep, save/load to SD |
| **Wi-Fi scan** | ESP32-S3 radio | Passive list of nearby access points (SSID / RSSI / channel / encryption) |
| **BLE scan** | ESP32-S3 radio | Passive list of nearby Bluetooth LE advertisers |
| **USB macro** | USB-C / HID | Run a Ducky-style keyboard macro from SD on your own computer |

Everything saves under `/czero/…` on a microSD card (optional — the UI works
screen-only without one).

## Your parts, and how they're used

From the kit you bought:

- **Cardputer Adv** — the computer: screen, keyboard, ESP32-S3, USB, microSD.
- **Cap CC1101 & NFC** — **the heart of this build.** Gives you Sub-GHz (CC1101)
  *and* 13.56 MHz NFC (ST25R3916), both over the shared SPI bus.
- **Mini IR Emitter & Receiver Unit** — IR learning + sending (Grove port).
- **RF 433 MHz TX Unit (SYN115)** — optional simple 433 MHz OOK transmitter
  (alternative to the CC1101; shares the Grove port).
- **I2C Hub (PCA9548A)** — optional; only needed if you later add several I²C
  Grove units at once.
- **NTAG203** and **MIFARE Classic 1K** tags — your test tags for the NFC tool.

Parts that don't earn a place in the default build, and why:

- **RFID Unit 2 (WS1850S)** — a *second* 13.56 MHz reader. The Cap's ST25R3916
  is far more capable (it reads NFC-A/B/F/V; the WS1850S is NFC-A/B detect only),
  so the Cap is used for NFC and the WS1850S is left out to avoid two readers
  fighting over the same job. The code is structured so it could be added later.
- **IR Reflective Sensor** — a proximity/line sensor, not an RF/NFC/IR tool;
  nothing in a Flipper-style device needs it.

See [`docs/WIRING.md`](docs/WIRING.md) for exactly what plugs in where, and why
you can only use one Grove unit at a time.

## Quick start

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. `git clone` this repo and open the folder.
3. Plug the Cardputer Adv in over USB-C.
4. Build & flash:
   ```bash
   pio run -e cardputer-adv -t upload
   ```
5. (Optional) format a microSD as FAT32 and insert it — the firmware makes the
   `/czero/…` folders on first boot.

Full build/flash notes, including entering download mode, are in
[`docs/BUILD_AND_FLASH.md`](docs/BUILD_AND_FLASH.md). How to drive each tool is in
[`docs/USAGE.md`](docs/USAGE.md).

## Controls

- **`;` / `.`** (or the up/down arrows) — move in menus
- **Enter** — select / confirm / act
- **`` ` ``** (backtick) or **Esc** — go back
- Letter keys trigger the shortcuts shown in each tool's footer (e.g. `s` save).

## Project layout

```
CardputerZero/
├── platformio.ini         build config + pinned libraries
├── partitions.csv         8 MB flash layout
├── src/
│   ├── config.h           ★ every pin + option lives here
│   ├── main.cpp           boot + top-level menu
│   ├── core/              UI, input, SPI-bus guard, SD storage, app base
│   └── apps/              subghz · nfc · ir · wifi_scan · ble_scan · badusb
├── data/                  example SD contents (IR button, USB macro)
├── diagrams/              wiring diagram (SVG)
└── docs/                  wiring · build/flash · usage · safety & legal
```

## Libraries (all official upstream, pinned in `platformio.ini`)

- [M5Unified](https://github.com/m5stack/M5Unified) + [M5Cardputer](https://github.com/m5stack/M5Cardputer)
- [M5Unit-NFC](https://github.com/m5stack/M5Unit-NFC) (M5UnitUnified) — ST25R3916 driver
- [RadioLib](https://github.com/jgromes/RadioLib) — CC1101 driver
- [IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote)

## License

MIT — see [`LICENSE`](LICENSE).
