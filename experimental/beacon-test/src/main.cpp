#include <ESP8266WiFi.h>

extern "C" {
  #include <user_interface.h>
}

const uint8_t channels[] = {1, 6, 11};
const bool wpa2 = true;
const bool appendSpaces = true;

const char ssids[] PROGMEM = {
  "D-TEST-01 Networking\n"
  "D-TEST-02 Hotspot X\n"
  "D-TEST-03 Free Wifi\n"
  "D-TEST-04 Test Beacon\n"
  "D-TEST-05 Fake Access\n"
  "D-TEST-06 Spam SSID 6\n"
  "D-TEST-07 Beacon Seven\n"
  "D-TEST-08 Eight Bits\n"
  "D-TEST-09 Nine Lives\n"
  "D-TEST-10 Ten Tenths\n"
};

char emptySSID[32];
uint8_t channelIndex = 0;
uint8_t macAddr[6];
uint8_t wifi_channel = 1;
uint32_t currentTime = 0;
uint32_t packetSize = 0;
uint32_t packetCounter = 0;
uint32_t attackTime = 0;
uint32_t packetRateTime = 0;

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

void nextChannel() {
  uint8_t ch = channels[channelIndex];
  channelIndex++;
  if (channelIndex >= (sizeof(channels) / sizeof(channels[0]))) channelIndex = 0;
  if (ch != wifi_channel && ch >= 1 && ch <= 14) {
    wifi_channel = ch;
    wifi_set_channel(wifi_channel);
  }
}

void randomMac() {
  for (int i = 0; i < 6; i++) {
    macAddr[i] = random(256);
  }
}

void setup() {
  for (int i = 0; i < 32; i++) emptySSID[i] = ' ';

  randomSeed(os_random());

  packetSize = sizeof(beaconPacket);
  if (wpa2) {
    beaconPacket[34] = 0x31;
  } else {
    beaconPacket[34] = 0x21;
    packetSize -= 26;
  }

  randomMac();

  Serial.begin(115200);
  Serial.println();
  Serial.println("=== BEACON SPAM v5 (spacehuhn replica) ===");

  currentTime = millis();

  WiFi.mode(WIFI_OFF);
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(channels[0]);

  Serial.println("SSIDs:");
  int i = 0;
  int len = sizeof(ssids);
  while (i < len) {
    Serial.print((char)pgm_read_byte(ssids + i));
    i++;
  }
  Serial.println("Started \\o/");
}

void loop() {
  currentTime = millis();

  if (currentTime - attackTime > 100) {
    attackTime = currentTime;

    int i = 0;
    int j = 0;
    int ssidNum = 1;
    char tmp;
    int ssidsLen = strlen_P(ssids);

    nextChannel();

    while (i < ssidsLen) {
      j = 0;
      do {
        tmp = pgm_read_byte(ssids + i + j);
        j++;
      } while (tmp != '\n' && j <= 32 && i + j < ssidsLen);

      uint8_t ssidLen = j - 1;

      macAddr[5] = ssidNum;
      ssidNum++;

      memcpy(&beaconPacket[10], macAddr, 6);
      memcpy(&beaconPacket[16], macAddr, 6);

      memcpy(&beaconPacket[38], emptySSID, 32);
      memcpy_P(&beaconPacket[38], &ssids[i], ssidLen);

      beaconPacket[82] = wifi_channel;

      if (appendSpaces) {
        for (int k = 0; k < 3; k++) {
          packetCounter += wifi_send_pkt_freedom(beaconPacket, packetSize, 0) == 0;
          delay(1);
        }
      } else {
        uint16_t tmpPacketSize = (packetSize - 32) + ssidLen;
        uint8_t* tmpPacket = new uint8_t[tmpPacketSize];
        memcpy(&tmpPacket[0], &beaconPacket[0], 38 + ssidLen);
        tmpPacket[37] = ssidLen;
        memcpy(&tmpPacket[38 + ssidLen], &beaconPacket[70], wpa2 ? 39 : 13);
        for (int k = 0; k < 3; k++) {
          packetCounter += wifi_send_pkt_freedom(tmpPacket, tmpPacketSize, 0) == 0;
          delay(1);
        }
        delete tmpPacket;
      }

      i += j;
    }
  }

  if (currentTime - packetRateTime > 1000) {
    packetRateTime = currentTime;
    Serial.print("Packets/s: ");
    Serial.println(packetCounter);
    packetCounter = 0;
  }
}