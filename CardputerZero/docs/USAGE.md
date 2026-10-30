# Using CardputerZero

Boot the device and you land on the main menu. Navigation everywhere:

- **`;` / `.`** or **↑ / ↓** — move
- **Enter** — select / act
- **`` ` ``** or **Esc** — back
- Letter keys = the shortcuts shown in each screen's footer.

Everything saved goes under `/czero/…` on the microSD card.

---

## Sub-GHz (CC1101)

For your own 315 / 433 / 868–915 MHz remotes and sensors (garage/gate remotes,
weather stations, doorbells you own).

1. **Set frequency** — pick the band your remote uses (check its label; most
   garage/gate remotes in the EU are 433.92 MHz).
2. **Receive (raw OOK)** — the device listens. Press your remote near it. When
   the burst ends you'll see how many edges were captured.
3. From the result screen: **Enter** replays it, **`s`** saves it to
   `/czero/subghz/` as a `.sub` text file.
4. **Replay last capture** re-sends whatever is in memory.
5. **Load from SD** brings a saved capture back so you can replay it.
6. **Frequency analyzer** sweeps RSSI across the presets — a quick way to see
   which band something is transmitting on (watch the bar jump when you press a
   remote).

> The capture is raw timing (OOK), which covers the vast majority of simple
> fixed-code remotes. It does **not** defeat rolling-code security (e.g. KeeLoq)
> — replaying a rolling-code remote won't work, by design.

## NFC / RFID (ST25R3916)

For your own 13.56 MHz tags — your **NTAG203** and **MIFARE Classic 1K** are
both supported.

1. Hold the tag flat against the **back of the Cap**.
2. **Enter** reads it: you'll see the type, UID, ATQA and SAK.
3. **`s`** appends the identity to `/czero/nfc/tags.csv`.
4. **`d`** does a full memory dump over **USB serial** — open the serial monitor
   to read it. (MIFARE Classic uses the default key `FF FF FF FF FF FF`; NTAG /
   Ultralight ignore keys.)

## Infrared

For your own TVs, projectors, air-con, etc.

- **Learn a button** — needs the **Mini IR Unit** (Grove) because the built-in
  LED can only send. Point a remote at the unit and press a key; it stores the
  decoded protocol, or raw timing if the protocol is unknown.
- **Replay learned** — sends it back (built-in LED by default; see WIRING.md to
  switch to the Grove emitter).
- **Universal TV power** — fires common Sony / RC5 / Samsung / NEC power codes
  in turn. Handy to switch your own TV; not every set responds.
- **`s`** saves the learned button to `/czero/ir/`; **Load from SD** restores it.

## Wi-Fi scan

Lists nearby access points (SSID, signal, channel, security). **Passive only**
— it never connects or interferes. **Enter** rescans.

## BLE scan

Lists nearby Bluetooth LE advertisers (name, signal, address). **Passive only**.
**Enter** rescans.

## USB macro (HID)

Runs a small keyboard script on **the computer the Cardputer is plugged into** —
for automating your own machine.

1. Put `.txt` scripts in `/czero/badusb/` on the SD card.
2. Pick one; a confirmation screen reminds you it types into this computer.
3. **Enter** confirms and runs it. **`` ` ``** aborts mid-run.

Script commands (one per line):

```
REM this is a comment
DEFAULTDELAY 50
DELAY 500
STRING hello world
STRINGLN this line presses enter after
GUI r              REM Win+R
CTRL c             REM Ctrl+C
ALT TAB
CTRL-ALT t         REM a terminal on many Linux desktops
ENTER / TAB / ESC / SPACE / UP / DOWN / LEFT / RIGHT
```

See `data/czero/badusb/hello.txt` for a working example.
