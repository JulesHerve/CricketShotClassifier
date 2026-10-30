# Build & flash

## Prerequisites

- [PlatformIO](https://platformio.org/) — either the VS Code extension or the
  CLI (`pip install platformio`).
- A USB-C cable and the Cardputer Adv.
- (Optional) a microSD card formatted **FAT32**.

The first build downloads the ESP32-S3 toolchain and the pinned libraries
(M5Unified, M5Cardputer, M5Unit-NFC, RadioLib, IRremote). This needs internet
access to PlatformIO's package registry and Espressif's download servers.

## Build

```bash
pio run -e cardputer-adv
```

## Flash

Plug the Cardputer in and:

```bash
pio run -e cardputer-adv -t upload
```

Then open the serial monitor (115200 baud):

```bash
pio device monitor -e cardputer-adv
```

### If upload fails — manual download mode

1. Turn the Cardputer **off** (power switch).
2. Hold **G0** (the `0` key boot button) down.
3. Turn it **on** while still holding G0, then release.
4. Run the upload command again.

### USB mode note

This firmware builds with `ARDUINO_USB_MODE=0` (TinyUSB/OTG) so the **USB macro**
tool can present an HID keyboard. Serial still works over the same port (CDC on
boot). After the USB-macro tool runs, the board stays a composite CDC+HID
device until reset — that's normal.

## Put example content on the SD card

Copy the `data/czero/` tree from this repo to the **root** of your FAT32 card:

```
/czero/ir/example_power.ir
/czero/badusb/hello.txt
```

The firmware creates the other folders (`/czero/subghz`, `/czero/nfc`, …) itself
on first boot. Everything you capture is written back under `/czero/…`.

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| "Radio init failed" in Sub-GHz | Cap not seated, or power-enable pin — reseat the Cap on the top connector |
| NFC "Reader init failed" | Same — the NFC chip is on the Cap's SPI bus |
| IR learn never captures | The built-in LED can't receive; plug in the **Mini IR Unit** (Grove) to learn |
| "no SD" everywhere | Card not FAT32, not seated, or formatted exFAT — reformat FAT32 |
| USB macro types nothing | Give the host a second to enumerate; some OSes need the device re-plugged once |

## Network note for cloud / CI builds

If you build inside a locked-down environment (e.g. a cloud agent), the build
needs outbound HTTPS to `api.registry.platformio.org`, `dl.platformio.org` and
`dl.espressif.com`. If those are blocked the toolchain can't download and the
build fails at "Installing espressif32". Build locally, or allow those hosts.
