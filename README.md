# ESP8266 Deauther + Beacon Spam

A Wemos D1 Mini (ESP8266) firmware that performs **Wi-Fi deauthentication** on all nearby access points and simultaneously broadcasts **fake beacon SSIDs** (beacon spam). This is an experimental project that has been field-tested and verified working.

> **Disclaimer:** This tool is for educational and authorized security testing purposes only. Unauthorized use against networks you do not own or have permission to test is illegal. The authors are not responsible for misuse.

## Features

- Deauth broadcast to all detected APs (reason code 7)
- Beacon spam with 50 funny/novelty SSIDs (visible on phones)
- TX pacing with `delay(1)` — zero packet drops (`fail=0`)
- TX-done callback (`wifi_register_send_pkt_freedom_cb`) for real RF transmission proof
- Channel hopping on `{1, 6, 11}` every 100ms
- Serial command interface (`/scan`, `/status`, `/help`)
- Real-time stats: ok/fail/tx-done/beacon/deauth/uptime
- LED indicator (fast blink = active)
- 160 MHz CPU overclock with `-O2` high-performance build flags (see Platform Details)

## Hardware Required

| Component | Details |
|-----------|---------|
| Wemos D1 Mini | ESP8266-based development board |
| Micro-USB cable | For power and programming |
| Computer | Windows (tested), macOS, or Linux |

That's it — no additional hardware needed.

## Software Prerequisites (Install from Scratch)

Follow these steps if you have a fresh computer with nothing installed.

### Step 1: Install Python 3

Python is required for PlatformIO and esptool.

1. Download Python 3.9+ from [python.org](https://www.python.org/downloads/)
2. Run the installer
3. **Important:** Check the box that says **"Add Python to PATH"** during installation
4. Verify installation:

```bash
python --version
# Should print: Python 3.x.x
```

### Step 2: Install PlatformIO CLI

PlatformIO is the build system used by this project.

```bash
pip install platformio
```

Verify:

```bash
platformio --version
# Should print: PlatformIO Core, version 6.x.x
```

> **Note:** On Windows you can also use `py -m platformio` if `platformio` is not in your PATH.

### Step 3: Install esptool (Optional)

esptool is only needed if you want to flash the pre-built `firmware.bin` directly instead of building from source.

```bash
pip install esptool
```

Verify:

```bash
esptool.py version
# Should print: esptool.py v4.x
```

## Project Setup

### Clone or Download

```bash
git clone <your-repo-url> deauther
cd deauther
```

Or download the ZIP and extract it.

### Install PlatformIO Dependencies

```bash
cd deauther
py -m platformio run
```

This will automatically download:
- `espressif8266@2.6.3` platform (Arduino core 2.7.4)
- ESP8266 framework files

First build takes a few minutes. Subsequent builds are fast.

## SDK Patch (Required)

The stock ESP8266 SDK **blocks** management frame transmission via `wifi_send_pkt_freedom`. You must replace the SDK library with a patched version that allows raw frame injection.

### Why Is This Needed?

Espressif's official SDK prevents sending deauth and beacon frames. The patched `libnet80211.a` removes this restriction. This project uses a patch from [SpacehuhnTech/arduino-esp8266](https://github.com/SpacehuhnTech/arduino-esp8266) (`deauther` branch).

### Apply the Patch

> **Quick install:** `install.bat` (Windows) and `install.sh` (Linux/macOS) do this step automatically — the patched library is bundled in this repo at `libnet80211.a.patched`; the scripts back up the stock file and replace it.

**1. Locate the SDK library directory:**

```
C:\Users\<YOUR_USERNAME>\.platformio\packages\framework-arduinoespressif8266@3.20704.0\tools\sdk\lib\NONOSDK22x_190703\
```

**2. Back up the original file:**

```bash
cd C:\Users\<YOUR_USERNAME>\.platformio\packages\framework-arduinoespressif8266@3.20704.0\tools\sdk\lib\NONOSDK22x_190703\
copy libnet80211.a libnet80211.a.stock.bak
```

**3. Use the bundled patched library:**

The repo ships the patched file at `libnet80211.a.patched` (336 KB, downloaded from the [SpacehuhnTech deauther branch](https://github.com/SpacehuhnTech/arduino-esp8266/tree/deauther)). Copy it over:

```bash
copy libnet80211.a.patched libnet80211.a
```

(If you need to re-download it, the direct URL is `https://raw.githubusercontent.com/SpacehuhnTech/arduino-esp8266/deauther/tools/sdk/lib/NONOSDK22x_190703/libnet80211.a`).

**4. Replace the file:**

```bash
copy <downloaded_path>\libnet80211.a libnet80211.a
```

**5. Verify:**

The patched file should now be at:
```
C:\Users\<YOUR_USERNAME>\.platformio\packages\framework-arduinoespressif8266@3.20704.0\tools\sdk\lib\NONOSDK22x_190703\libnet80211.a
```

> **Warning:** If PlatformIO updates the framework package, you will need to re-apply the patch.

## Build & Flash

### Option A: Build and Flash with PlatformIO

```bash
cd deauther
py -m platformio run
```

The firmware binary will be at `.pio/build/d1mini/firmware.bin`.

> **Windows Defender note:** `install.bat` builds in `%TEMP%\deauther_build` automatically, because Windows **Controlled Folder Access** (when enabled) silently blocks the ESP8266 toolchain binaries from writing `.pio\` inside a protected folder like `Documents`. If you build manually inside `Documents` and see `cc1.exe: fatal error: opening output file ... No such file or directory`, build from `%TEMP%` instead (see Troubleshooting below).

### Option B: Flash Pre-built Binary

A pre-built `firmware.bin` is included in the repo. Flash it directly:

```bash
# Find your COM port (check Device Manager)
# Windows:
py -m esptool --port COM6 --baud 115200 write_flash 0x0 firmware.bin

# Linux/macOS:
esptool.py --port /dev/ttyUSB0 --baud 115200 write_flash 0x0 firmware.bin
```

### Finding Your COM Port

**Windows:**
1. Open Device Manager
2. Expand "Ports (COM & LPT)"
3. Look for "CP210x USB to UART Bridge" or "CH340" — note the COM number (e.g., COM6)

**Linux:**
```bash
ls /dev/ttyUSB* /dev/ttyACM*
# Usually /dev/ttyUSB0
```

**macOS:**
```bash
ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART*
```

### Flashing Troubleshooting

| Problem | Solution |
|---------|----------|
| `Failed to connect` | Hold FLASH button on Wemos while running esptool, then release |
| `Permission denied` (Linux/macOS) | `sudo chmod 666 /dev/ttyUSB0` or add your user to `dialout` group |
| `Could not open COM port` | Close any Serial Monitor / other programs using the port |
| `A fatal error occurred: Wrong boot mode` | Hold FLASH, press RESET, release RESET, then release FLASH |

## Serial Commands

Connect at **115200 baud** using any serial monitor (PlatformIO Serial Monitor, Arduino Serial Monitor, PuTTY, etc.):

```bash
py -m platformio device monitor
```

| Command | Description |
|---------|-------------|
| `/scan` | Rescan networks and update AP list |
| `/status` | Show current stats (APs, channel, packets, uptime) |
| `/help` | List available commands |

The device also prints real-time stats every 3 seconds automatically.

## How It Works

### Attack Cycle (Every 100ms)

1. **Channel hop** — cycles through channels `{1, 6, 11}`
2. **Deauth burst** — sends 10 deauth frames per AP on the current channel
3. **Beacon spam** — broadcasts all 50 SSIDs (each sent 3 times)
4. **Repeat** — approximately 900-1000 packets/second total

### Frame Types

| Frame | Type Code | Purpose |
|-------|-----------|---------|
| Beacon | `0x80` | Fake SSID broadcast (spam) |
| Deauthentication | `0xC0` | Disconnect clients from AP (reason code 7) |

### Beacon Frame Structure

- 109-byte template based on [spacehuhn/esp8266_beaconSpam](https://github.com/spacehuhn/esp8266_beaconSpam)
- Timestamp fixed, beacon interval 1s (`0xe8, 0x03`)
- Capability `0x31` + full RSN IE (WPA2)
- Supported rates: `0x82 0x84 0x8b 0x96 0x24 0x30 0x48 0x6c`
- SSID padded to 32 chars, random MAC with sequential last byte

### Key Fix: Why Beacons Now Appear on Phones

Early versions showed `wifi_send_pkt_freedom` returning `0` (success) but beacons were invisible on phones. The `ok` counter is **not** proof of RF transmission. The fix:

1. **`WiFi.mode(WIFI_OFF)` before `wifi_set_opmode(STATION_MODE)`** — clean RF initialization on cold boot
2. Exact beacon format matching the reference (RSN IE, 1s interval, supported rates)
3. Channel hopping on `{1, 6, 11}` only (not all AP channels)
4. No promiscuous mode on the TX path

## Troubleshooting

### Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Platform not installed` | Missing espressif8266 platform | Run `py -m platformio pkg install -g -p espressif8266@2.6.3` |
| `Framework not found` | Missing Arduino ESP8266 framework | PlatformIO should auto-download; check internet connection |
| `Permission denied` on `.pio` | Windows ACL/permission issue | Build from a temp directory or run terminal as admin |
| `cc1.exe: fatal error: opening output file ... No such file or directory` even though the folder exists | Windows Defender **Controlled Folder Access** blocking the compiler from writing into a protected folder (`Documents`, `Desktop`, ...) | Build from `%TEMP%` (not protected) — `install.bat` does this automatically — or add the toolchain folder to Defender's allowed apps (Windows Security → Virus & threat protection → Ransomware protection → Controlled folder access → Allow an app) |

### Runtime Issues

| Issue | Fix |
|-------|-----|
| Beacon spam not visible on phone | Ensure SDK patch is applied and `WiFi.mode(WIFI_OFF)` runs before `wifi_set_opmode` |
| `fail` count increasing | Remove `delay(1)` between packets or check for interference |
| LED not blinking | GPIO2 (built-in LED) — verify pin mapping for your board |
| Serial garbled | Ensure baud rate is set to 115200 |

## Project Files

| File | Description |
|------|-------------|
| `src/main.cpp` | Main firmware source (deauth + beacon spam) |
| `firmware.cpp` | Alternative/earlier firmware (deauth only, simpler) |
| `firmware.bin` | Pre-built binary ready for flashing |
| `libnet80211.a.patched` | Patched SDK library (bundled, used by installers) |
| `install.bat` | One-click installer for Windows |
| `install.sh` | One-click installer for Linux/macOS |
| `platformio.ini` | PlatformIO build configuration |
| `experimental/beacon-spam/` | Experimental beacon spam iterations |
| `experimental/beacon-test/` | Test firmware (beaconSpam replica, RX channel proof) |

## Platform Details

| Component | Version |
|-----------|---------|
| PlatformIO platform | `espressif8266@2.6.3` |
| Arduino core | 2.7.4 |
| ESP8266 SDK | 2.2.1 (with patched `libnet80211.a`) |
| Board | `d1_mini` (Wemos D1 Mini) |
| CPU frequency | 160 MHz (`board_build.f_cpu = 160000000L`) |
| Build flags | `-O2 -fno-exceptions -ffunction-sections -fdata-sections` |

### Build Usage (Measured)

With the 160 MHz config and `-O2` flags, a clean build reports:

- RAM: 33.9% (27,772 / 81,920 bytes)
- Flash: 26.0% (271,080 / 1,044,464 bytes)

This leaves ample headroom; the firmware is transmission-rate limited by the
1 Mbps management-frame airtime, not by the CPU.

## Credits & References

- [SpacehuhnTech/arduino-esp8266](https://github.com/SpacehuhnTech/arduino-esp8266) — patched SDK library
- [spacehuhn/esp8266_beaconSpam](https://github.com/spacehuhn/esp8266_beaconSpam) — beacon frame reference
- [SpacehuhnTech/ESP8266 Deauther](https://github.com/SpacehuhnTech/ESP8266-Deauther) — original deauther project
- [ESP8266 Packet Injection](https://github.com/SpacehuhnTech/esp8266_deauther/wiki/802.11-Packet-Injection) — technical background

## License

This project is for educational purposes. Use responsibly and only on networks you own or have explicit permission to test.
