# ESP8266 Deauther Engine Upgrade — Design Spec

Date: 2026-08-18
Project: ESP8266EX-D (ESP8266 Deauther + Beacon Spam firmware, Wemos D1 Mini)

## Purpose

Upgrade the existing single-file firmware (`src/main.cpp`) to run at the
802.11 management-frame airtime limit with zero dropped packets, add
multi-vector attack modes (multi-reason deauth, disassoc, targeted AP
lockdown, evil twin clone), and add viral beacon packs (Rickroll lyrics
sequence, animated marquee) on top of the existing meme pack.

This is a dual-use security-testing tool for use only on networks the
operator owns or has explicit permission to test (existing README
disclaimer retained).

## Decisions (from brainstorming)

| Topic | Decision |
|---|---|
| Attack start | Auto-attack on boot (current behavior kept) |
| Performance goal | Max pps at the RF airtime limit with zero drops; NOT a literal 10x (management frames are 1 Mbps; physically capped ~1.2k beacon pps / ~3.5k deauth pps) |
| SSID packs | Keep existing 50-SSID meme pack; add Rickroll sequence + marquee. Emoji pack explicitly NOT included |
| Architecture | Single-file rewrite of `src/main.cpp` with clear sections |
| CLI | Full command set implemented; `/help` documents only `/scan /stop /reboot /status /help`; all other commands work but are undocumented |
| Verification | Build locally with PlatformIO; copy to `%TEMP%` if Controlled Folder Access blocks `.pio` writes |

## 1. Attack Engine

### Boot flow
1. `Serial.begin(115200)`, banner
2. `pinMode(LED_BUILTIN, OUTPUT)`
3. `WiFi.mode(WIFI_OFF)` then `wifi_set_opmode(STATION_MODE)` (RF-clean init)
4. `system_update_cpu_freq(160)`
5. `wifi_set_channel(1)`, register TX-done callback
6. Initial scan
7. Auto-start attack loop (no user action required)

### Attack modes (runtime-switchable, undocumented commands)
- `MIXED` (default) — deauth/disassoc on all scanned APs of the current
  channel + beacon spam from the active pack
- `DEAUTH` — deauth/disassoc only
- `BEACON` — beacon spam only
- `EVIL_TWIN <n>` — clone scanned AP #n: same SSID, BSSID with last MAC
  byte flipped, high-power beacon flood with the cloned identity + targeted
  deauth against the real AP
- `TARGETED <bssid>` — AP lockdown: deauth/disassoc only on that BSSID
  (broadcast + per-client frames), no beacon spam

### Multi-vector frames (alternated across bursts)
- Deauth `0xC0` — reason 7 (Class 3 frame from nonassociated STA)
- Deauth `0xC0` — reason 2 (previous auth no longer valid)
- Deauth `0xC0` — reason 1 (unspecified)
- Disassoc `0xA0` — reason 8 (sending STA leaving BSS)

### Channel hopping
- `SMART` (default) — cycle only channels with discovered APs
- `SWEEP` — full 1–14 sweep
- 100 ms hop interval: hop → burst per cycle
- Auto-rescan every 60 s to refresh the AP list

## 2. Performance Engine

### CPU & build
- `system_update_cpu_freq(160)` at boot
- `platformio.ini`: `board_build.f_cpu = 160000000L`, build flags
  `-O2 -fno-exceptions -ffunction-sections -fdata-sections`

### Pre-built frames (zero-copy, no heap)
- One reusable 109-byte beacon buffer built once in RAM; per-beacon only
  the changing bytes are patched: SSID bytes + length, MACs, channel byte,
  timestamp, sequence
- One reusable 26-byte deauth/disassoc buffer; per-send only reason code
  and target MACs are patched
- No `memcpy` in the hot path, no `String`, no heap allocation after boot
- SSID data in PROGMEM; copied to the beacon buffer only when the SSID
  actually changes

### Non-blocking TX pacing
- No `delay(1)` between sends; pump `wifi_send_pkt_freedom` as fast as it
  accepts
- `ESP.wdtFeed()` periodically so the watchdog never trips
- Nonzero return (queue full) = natural backpressure: counted `busy`
  (not `fail`), then `delay(0)`/`yield()` briefly
- `fail` counts only real API errors; TX-done callback proves RF
  transmission
- Target: ~1,100–1,200 beacon pps / ~3,000+ deauth pps, `fail=0`

### Stats
`ok` / `busy` / `fail` / `tx_done` / per-mode counters / pps (1 s window)
/ uptime — auto-reported every 3 s and via `/status`.

## 3. Viral Beacon Packs

All data in PROGMEM; one active pack at a time (default: meme pack).
Runtime switch via undocumented command.

1. **Meme pack** — existing 50 SSIDs, unchanged.
2. **Rickroll sequence** — full "Never Gonna Give You Up" lyrics, line by
   line, each line ≤32 bytes (long lines split across two SSIDs). Two
   playout modes:
   - SEQ — one lyric line repeated ~8 s, then advance to next line
   - ALL — all lyric lines broadcast in one burst
3. **Marquee** — one animated scrolling SSID (default text
   `★ DEAUTHER ONLINE ★`), shifted left one char every ~200 ms, ≤32 bytes.

## 4. Serial CLI

115200 baud. Full command set implemented; `/help` documents only the
subset.

| Command | Action | Documented |
|---|---|---|
| `/scan` | Rescan, refresh AP list | yes |
| `/status` | Stats: mode, pack, channel, pps, ok/busy/fail/tx, uptime | yes |
| `/help` | List documented commands | yes |
| `/stop` | Pause attacks | yes |
| `/reboot` | Software reset | yes |
| `/mode mixed|deauth|beacon` | Switch attack mode | no |
| `/target <bssid>` / `/target off` | Targeted AP lockdown | no |
| `/twin <n>` / `/twin off` | Evil twin clone | no |
| `/pack memes|rickroll|marquee` | Active beacon pack | no |
| `/rickroll seq|all` | Rickroll playout mode | no |
| `/marquee <text>` | Set scrolling text | no |
| `/hop smart|sweep` | Hopping strategy | no |
| `/rate <pps>` | TX rate cap (0 = max) | no |

Parse errors print usage hints; unknown commands reported. Commands are
non-blocking, parsed in `loop()`, never inside the TX path.

## 5. Build Config & Docs

### platformio.ini
```ini
[env:d1mini]
platform = espressif8266@2.6.3
board = d1_mini
framework = arduino
monitor_speed = 115200
board_build.f_cpu = 160000000L
build_flags =
  -O2
  -fno-exceptions
  -ffunction-sections
  -fdata-sections
```

### README.md updates
- Realistic benchmark specs (measured pps per mode, fail=0, RAM/flash
  usage from build log)
- Attack modes & multi-reason frame table
- Serial command reference (documented subset; note that additional
  undocumented commands exist)
- Install/flash/SDK-patch workflow unchanged

## 6. Verification

1. `py -m platformio run` in-project; on Defender `.pio` write failure,
   build from a `%TEMP%` copy (per existing README guidance)
2. Flash/RAM usage from build log must be comfortably under limits
3. On-device (user hardware):
   - `/status` shows `ok` climbing at RF-limit rate, `fail=0`, low `busy`
   - `/stop` halts; `/reboot` resets
   - Beacons appear on a phone; Rickroll advances line-by-line; marquee
     scrolls
4. Success claims limited to what build output and counters prove
