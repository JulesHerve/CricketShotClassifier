# Safety, legality & responsible use

CardputerZero is a learning and automation tool for **hardware, signals, tags
and computers you own or have explicit permission to test**. Radios and NFC are
fun to explore — and in most places they're also regulated. Read this once.

## The one rule

> **Only transmit to, read from, or control things that are yours, or that you
> have the owner's clear permission to work on.**

That covers your garage remote, your TV, your own RFID tags, your own PC. It
does **not** cover your neighbour's car, a shop's access card, or a network you
don't run.

## Radio transmission (Sub-GHz & IR & 433)

- Transmitting on 315 / 433 / 868–915 MHz is allowed **only** in specific bands,
  power levels and duty cycles that **vary by country** (FCC Part 15 in the US,
  ETSI EN 300 220 / short-range-device rules in Europe, etc.). The Cap's
  hardware restricts you to three bands, but staying legal is still your
  responsibility.
- **Don't jam.** Continuously transmitting to drown out other signals is illegal
  almost everywhere. This firmware has no jammer and you shouldn't add one.
- Keep transmissions short and only when testing your own gear.

## Rolling codes

Replaying a captured Sub-GHz burst works on **fixed-code** remotes. Modern
garage doors, car fobs and alarms use **rolling codes** (KeeLoq and similar) that
change every press — a replay won't open them, by design. CardputerZero does not
try to break rolling-code security, and you shouldn't attempt to against
anything you don't own.

## NFC / RFID

- Reading a tag's UID is like reading a barcode — harmless on your own tags.
- **Cloning** an access card to get into somewhere you're not authorised is
  fraud/trespass in most jurisdictions. Don't. Use this to understand your own
  tags, build your own projects, and learn how the tech works.
- Many secure cards (bank cards, modern transit, DESFire with keys you don't
  have) can't be cloned anyway.

## Wi-Fi & BLE

The scanners here are **passive** — they listen to the beacons every device
already broadcasts. There is intentionally **no** deauthentication, no evil-twin,
no packet injection. Listening is generally fine; don't extend this toward
disrupting or impersonating networks.

## USB macro (HID)

The USB-macro tool types into whatever computer the Cardputer is plugged into.
Run it on **your own machine**. Using a "rubber-ducky" style payload against
someone else's computer without authorisation is unauthorised access — a crime
in most countries.

## No warranty

This is a hobby project provided under the MIT license **as-is**. You are
responsible for how you build and use it, and for complying with the radio,
privacy and computer-misuse laws where you are.
