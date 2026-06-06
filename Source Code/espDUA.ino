#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>

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

// Tombol pakan manual
const int tombolPakan = 14;     // D14 / GPIO14

// ================= SERVO PAKAN =================
Servo servoPakan;
const int pinServoPakan = 18;

const int SUDUT_TUTUP = 0;
const int SUDUT_BUKA  = 90;

const unsigned long DURASI_BUKA_PAKAN_MS = 2000;
bool pakanAktif = false;
unsigned long waktuMulaiPakan = 0;

// ================= INTERRUPT =================
volatile bool triggerModeRelay = false;
volatile bool triggerPakanManual = false;

// Debounce tetap dipakai supaya sekali pencet tidak terkirim berkali-kali
volatile unsigned long waktuInterruptModeTerakhir = 0;
volatile unsigned long waktuInterruptPakanTerakhir = 0;
const unsigned long DEBOUNCE_INTERRUPT_US = 300000;

// ================= ISR MODE RELAY =================
void IRAM_ATTR interruptModeRelay() {
  unsigned long sekarang = micros();

  if (sekarang - waktuInterruptModeTerakhir > DEBOUNCE_INTERRUPT_US) {
    triggerModeRelay = true;
    waktuInterruptModeTerakhir = sekarang;
  }
}

// ================= ISR PAKAN =================
void IRAM_ATTR interruptPakan() {
  unsigned long sekarang = micros();

  if (sekarang - waktuInterruptPakanTerakhir > DEBOUNCE_INTERRUPT_US) {
    triggerPakanManual = true;
    waktuInterruptPakanTerakhir = sekarang;
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

// ================= SERVO PAKAN =================
void mulaiPakan() {
  if (!pakanAktif) {
    pakanAktif = true;
    waktuMulaiPakan = millis();

    servoPakan.write(SUDUT_BUKA);
    Serial.println("Servo pakan BUKA");
  }
}

void updatePakan() {
  if (pakanAktif) {
    if (millis() - waktuMulaiPakan >= DURASI_BUKA_PAKAN_MS) {
      servoPakan.write(SUDUT_TUTUP);
      pakanAktif = false;

      Serial.println("Servo pakan TUTUP");
    }
  }
}

// ================= UPDATE TOMBOL =================
void updateTombol() {
  if (triggerModeRelay) {
    triggerModeRelay = false;
    kirimModeRelay();
  }

  if (triggerPakanManual) {
    triggerPakanManual = false;
    mulaiPakan();
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println();
  Serial.print("MAC ESP KEDUA: ");
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

  // Tombol
  pinMode(tombolModeRelay, INPUT_PULLUP);
  pinMode(tombolPakan, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(tombolModeRelay), interruptModeRelay, FALLING);
  attachInterrupt(digitalPinToInterrupt(tombolPakan), interruptPakan, FALLING);

  // Servo
  servoPakan.attach(pinServoPakan);
  servoPakan.write(SUDUT_TUTUP);

  Serial.println("ESP kedua siap");
  Serial.println("D13/GPIO13 = tombol mode relay");
  Serial.println("D14/GPIO14 = tombol pakan");
  Serial.println("GPIO18 = signal servo pakan");
}

// ================= LOOP =================
void loop() {
  updateTombol();
  updatePakan();
}