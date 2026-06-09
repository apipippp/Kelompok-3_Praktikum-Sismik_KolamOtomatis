#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ================= MAC ESP UTAMA =================
// GANTI sesuai MAC ESP utama
uint8_t alamatReceiver[] = {0x38, 0x18, 0x2b, 0x8a, 0x88, 0xdc};

// ================= DATA ESP-NOW =================
typedef struct pesan_t {
  char command;
} pesan_t;

pesan_t dataKirim;

// ================= PIN TOMBOL ESP KEDUA =================
// Tombol mode relay, kirim ke ESP utama
const int tombolModeRelay = 13; // D13 / GPIO13

// Kalau tombol kamu pakai GPIO19, ganti menjadi:
// const int tombolModeRelay = 19;

// ================= INTERRUPT =================
volatile bool triggerModeRelay = false;

// Debounce agar sekali pencet tidak terkirim berkali-kali
volatile unsigned long waktuInterruptModeTerakhir = 0;
const unsigned long DEBOUNCE_INTERRUPT_US = 300000;

// ================= ISR MODE RELAY =================
void IRAM_ATTR interruptModeRelay() {
  unsigned long sekarang = micros();

  if (sekarang - waktuInterruptModeTerakhir > DEBOUNCE_INTERRUPT_US) {
    triggerModeRelay = true;
    waktuInterruptModeTerakhir = sekarang;
  }
}

// ================= KIRIM ESP-NOW =================
void kirimModeRelay() {
  dataKirim.command = 'R';

  esp_err_t hasil = esp_now_send(alamatReceiver, (uint8_t *) &dataKirim, sizeof(dataKirim));

  if (hasil == ESP_OK) {
    Serial.println("Kirim command R berhasil");
  } else {
    Serial.println("Kirim command R gagal");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println();
  Serial.print("MAC ESP DUA: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW gagal init!");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, alamatReceiver, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Gagal tambah peer ESP utama!");
    return;
  }

  // Setup tombol
  pinMode(tombolModeRelay, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(tombolModeRelay), interruptModeRelay, FALLING);

  Serial.println("ESP DUA siap");
  Serial.println("Tombol interrupt -> ESP-NOW -> ESP utama");
}

// ================= LOOP =================
void loop() {
  if (triggerModeRelay) {
    triggerModeRelay = false;

    kirimModeRelay();
  }
}