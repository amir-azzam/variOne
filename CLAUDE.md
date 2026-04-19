# VariOne — Context for Claude Code

## Project

Graduation project at Canadian International College (CIC) Cairo, supervised by Dr. Ahmed Gaber (cybersecurity). A multi-tool pentesting device for teaching engineering students about wireless vulnerabilities in a controlled lab environment. Current firmware: v0.4 (stable, working).

Repo: github.com/amir-azzam/variOne
Tags: v0.1 (original), v0.2 (beacon spam + buttons), v0.3 (evil twin), v0.4 (mascot system)

## Dev environment

- PlatformIO + VS Code on Fedora 43 (some sessions on Windows too — paths like C:/Users/Dena/ appear)
- Board: esp32dev, framework=Arduino
- Port: /dev/ttyUSB0 on Linux, COM port on Windows
- Main library: U8g2 for OLED

## Hardware inventory

- ESP32 DevKit V1 CH340 USB-C
- SH1106 1.3" OLED 128x64 blue, I2C — SDA=21, SCL=22
- CC1101 433MHz (E07-M1101D V2.0) — already soldered, antenna attached, has female-to-male jumpers
- PN532 NFC (not yet wired) — I2C mode SEL0=1 SEL1=0, address 0x24
- IR receiver VS1838B on GPIO 36 (IR transmitter LED 940nm + 100Ω resistor not yet purchased)
- 4 tactile buttons — Left=14, Up=26, Right=32, Down=33, all INPUT_PULLUP
- SD card module (not yet wired) — CS=GPIO 5
- MB102 breadboard power supply, both rails set to 3.3V

## Pin map (current and planned)

```
GPIO 21     OLED SDA (I2C)
GPIO 22     OLED SCL (I2C)
GPIO 14     Button LEFT   (INPUT_PULLUP)
GPIO 26     Button UP     (INPUT_PULLUP)
GPIO 32     Button RIGHT  (INPUT_PULLUP)
GPIO 33     Button DOWN   (INPUT_PULLUP)
GPIO 18     SPI SCK   (shared bus: CC1101 + future SD)
GPIO 19     SPI MISO  (shared)
GPIO 23     SPI MOSI  (shared)
GPIO 27     CC1101 CSN (safe-boot HIGH required before SPI init)
GPIO 4      CC1101 GDO0 (newly added for this build)
GPIO 5      SD card CS (future — move SD to HSPI to avoid conflict, see notes)
GPIO 36     IR receiver
GPIO 25     IR transmitter (future)
```

CC1101 module pin-to-GPIO wiring (E07-M1101D V2.0, verified against Ebyte datasheet):

```
Module pin 1 (GND)  -> ESP32 GND
Module pin 2 (VCC)  -> ESP32 3.3V   (never 5V — destroys chip)
Module pin 3 (GDO0) -> ESP32 GPIO 4
Module pin 4 (CSN)  -> ESP32 GPIO 27
Module pin 5 (SCK)  -> ESP32 GPIO 18
Module pin 6 (MOSI) -> ESP32 GPIO 23
Module pin 7 (MISO) -> ESP32 GPIO 19
Module pin 8 (GDO2) -> leave unconnected
```

## Current firmware features (all stable in v0.4)

**Core features (the project's actual pitch):**

- WiFi Scanner (async, mascot waves during scan, cancelable with back button)
- Packet Monitor (promiscuous mode, 14-channel hop 200ms, animated bar graph)
- Probe Sniffer (0x40 frames, deduped, 500ms hop)
- Deauth Detector (passive 0xC0/0xA0 sniff, 300ms hop)
- Beacon Spam (20 fake SSIDs, unique MACs per SSID, channels 1/6/11)
- Evil Twin AP (clones target SSID, open network, DNS hijack, CIC PowerCampus portal clone with dark red #7B1818, credentials captured to serial + SD)
- Mascot system (10 moods, boot animation, 30s idle sleep, mood reactions on key events)
- SD card logging (graceful fallback if no card, 'r' command in serial dumps all creds)

**Non-core / test-only:**

- Deauth Attack — code compiles and transmits, but blocked by PMF/802.11w on modern devices. Needs a WPA2-only router with PMF disabled (2.4GHz, not a phone hotspot) to validate. Not a main selling point; included for completeness of the wireless-vulnerability demonstrations and only demonstrated on lab hardware.

## Ethics framing (important for supervisor review)

This is a legitimate academic project for demonstrating wireless vulnerabilities on **lab-owned equipment only**. Not for attacking random targets. Egyptian Law 175/2018 on cybercrime makes unauthorized RF access a criminal offense, and practically most modern devices defeat naive replay anyway.

Sub-GHz features will target fixed-code devices (PT2262 remotes, cheap doorbells, 433MHz weather stations) that the lab owns, in controlled conditions. The pedagogical story is "why fixed-code RF is insecure" — the classic Samy Kamkar framing. This is the angle that passes ethics review and is publishable.

## Current task: Sub-GHz (CC1101) bring-up

Planned features once hardware is confirmed:

1. Spectrum Analyzer — sweep 300-348 / 387-464 / 779-928 MHz bands, OLED RSSI bar graph per channel, find active transmitters
2. Signal Capture + Replay — fixed-code only, captures OOK signals from lab devices, shows decoded bits on OLED, replay on button press
3. Protocol Analyzer — rc-switch decoder, live protocol/bits/value display
4. RSSI Live Meter — lock to user-chosen frequency, show live RSSI (fox-hunt mode)
5. Jamming Detector — stretch feature, defensive tool that alerts when noise floor on 433.92MHz spikes

Libraries: `lsatan/SmartRC-CC1101-Driver-Lib` + `sui77/rc-switch`

## Current blocker

CC1101 wiring attempt caused the OLED to stop working mid-session (symptoms: stuck on boot splash, never reached menu). Removed CC1101 entirely — main firmware now builds and runs fine again. Need to re-attempt the wiring more carefully.

Last smoke test result before things broke: `CC1101: Connection error` — never got a valid PARTNUM/VERSION response from the chip. Suspected causes in order of likelihood:

1. Loose female-to-male jumper on one or more of the 7 wires
2. MISO/MOSI swap
3. Power brownout (CC1101 + OLED + ESP32 sharing weak supply)
4. Wrong pin on module — but pinout was verified against Ebyte V2.0 datasheet, matches what we wired

## Recommended next steps

1. Confirm main.cpp firmware still runs cleanly (OLED boot animation, menu navigation, WiFi scan all work)
2. Re-wire CC1101 carefully — multimeter-verify 3.3V on VCC pin, press every jumper firmly, verify no pins are shorted to neighbors
3. Use a separate PlatformIO env (`cc1101_test`) with `src/cc1101_test.cpp` to probe the chip via raw SPI — do NOT touch main.cpp until the smoke test responds correctly
4. Target expected output: `CC1101: Connection OK` with PARTNUM=0x00 and VERSION=0x04 or 0x14
5. Only integrate Sub-GHz features into main.cpp once the smoke test passes
6. When adding SD card later: move SD to HSPI (SCK=14, MISO=12, MOSI=13, CS=15) to avoid the known SmartRC issue #40 conflict on shared VSPI

## Useful technical details

- `esp_log_level_set("wifi", ESP_LOG_NONE)` silences wifi driver logs
- Safe boot for CC1101: `pinMode(27, OUTPUT); digitalWrite(27, HIGH)` must be called BEFORE any SPI activity — prevents bus conflict during boot
- `IRAM_ATTR` on all promiscuous callbacks
- Deauth attack uses `WIFI_AP_STA + softAP("v", nullptr, ch, 1)` hidden to unlock WIFI_IF_AP for esp_wifi_80211_tx
- Beacon frame built manually, 100 byte buffer, channels 1/6/11
- Evil Twin: DNSServer port 53 wildcard + WebServer port 80, captive portal endpoints for Android/iOS/Windows
- Credentials saved to /creds.txt on SD, appended with timestamp
- CC1101 library requires `setSpiPin()` and `setGDO0()` calls BEFORE `Init()`
- Mascot: 16 poses in design files, drawn via U8g2 primitives, blue/white robot-panda with visor, VariOne logo on chest

## Mascot mood triggers

```
WiFi found          -> HAPPY
WiFi not found      -> SAD
Scan running        -> WAVING (animated arm)
Packet monitor      -> WORKING
Probe found         -> HAPPY
Client scan         -> THINKING
Deauth start        -> ANGRY
Beacon spam         -> HAPPY
Evil Twin up        -> WORKING
Credential captured -> SUCCESS
```

Available moods: IDLE, HAPPY, THINKING, SAD, ANGRY, SLEEPING, SUCCESS, FAIL, WORKING, WAVING

## Documentation status

- IEEE 1016-2009 compliant design doc exists (35 pages)
- Presentation slides done
- 3D printed enclosure under consideration for final presentation
- No README yet (this file is the closest thing — can be extended into one)

## How Claude Code should work on this

- Always confirm main.cpp builds before touching it
- Use the separate test environment for any hardware bring-up (SPI probes, I2C scans, etc)
- Preserve the v0.4 feature set — never regress working functionality
- When adding new features, follow the existing pattern: AppState enum entry, a draw function, a start/stop pair, and menu integration
- Mascot mood triggers should be added for any new user-facing action
- SD logging should be optional (graceful fallback if card missing)
