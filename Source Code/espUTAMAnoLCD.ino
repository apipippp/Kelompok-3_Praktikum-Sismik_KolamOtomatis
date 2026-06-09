#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// ================= PIN ESP32 UTAMA =================
const int pinPot = 34;

const int ledBuang = 26;
const int ledIsi = 27;
const int ledKeruh = 14;

// Relay 2 channel
const int relayBuang = 19;
const int relayIsi   = 23;

// Ultrasonik
const int pinTrig = 32;
const int pinEcho = 33;

// ================= SETTING RELAY NO =================
// Kebanyakan relay module aktif LOW
const int RELAY_AKTIF = LOW;
const int RELAY_MATI  = HIGH;

// Karena pakai terminal NO:
// Pompa ON  = relay aktif
// Pompa OFF = relay tidak aktif
const int POMPA_ON  = RELAY_AKTIF;
const int POMPA_OFF = RELAY_MATI;

// ================= DATA ESP-NOW =================
typedef struct pesan_t {
  char command;
} pesan_t;

pesan_t dataMasuk;

// Queue untuk command dari ESP-NOW
QueueHandle_t queueCommand;

// ================= PARAMETER AIR =================
int batasKeruh = 2000;

// Batas ultrasonik
const int BATAS_MULAI_ISI_CM = 20; // jika jarak > 20 cm, mulai isi
const int BATAS_STOP_ISI_CM  = 15; // jika jarak <= 19 cm, stop isi

// Data sensor global
volatile int nilaiADC = 0;
volatile int jarak = 999;

// Status air
int statusAir = 0;
// 0 = NORMAL
// 1 = KURAS
// 2 = ISI

// Mode manual
int modeManual = 0;
// 0 = AUTO
// 1 = MANUAL BUANG
// 2 = MANUAL ISI
// 3 = MANUAL OFF

// ================= JEDA KURAS KE ISI =================
unsigned long waktuMulaiJedaIsi = 0;
const unsigned long JEDA_SEBELUM_ISI_MS = 2000;
bool sedangJedaIsi = false;

// ================= SERIAL MONITOR UPDATE =================
unsigned long waktuUpdateSerial = 0;
const unsigned long INTERVAL_SERIAL_MS = 500;

// ================= CALLBACK ESP-NOW =================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(dataMasuk)) {
    memcpy(&dataMasuk, incomingData, sizeof(dataMasuk));

    if (dataMasuk.command == 'R') {
      char cmd = 'R';
      xQueueSend(queueCommand, &cmd, 0);
    }
  }
}
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(dataMasuk)) {
    memcpy(&dataMasuk, incomingData, sizeof(dataMasuk));

    if (dataMasuk.command == 'R') {
      char cmd = 'R';
      xQueueSend(queueCommand, &cmd, 0);
    }
  }
}
#endif

// ================= FUNGSI RELAY =================
void semuaPompaOff() {
  digitalWrite(relayBuang, POMPA_OFF);
  digitalWrite(relayIsi, POMPA_OFF);

  digitalWrite(ledBuang, LOW);
  digitalWrite(ledIsi, LOW);
}

void pompaBuangOn() {
  digitalWrite(relayBuang, POMPA_ON);
  digitalWrite(relayIsi, POMPA_OFF);

  digitalWrite(ledBuang, HIGH);
  digitalWrite(ledIsi, LOW);
}

void pompaIsiOn() {
  digitalWrite(relayBuang, POMPA_OFF);
  digitalWrite(relayIsi, POMPA_ON);

  digitalWrite(ledBuang, LOW);
  digitalWrite(ledIsi, HIGH);
}

// ================= FUNGSI BACA JARAK =================
int bacaJarak() {
  digitalWrite(pinTrig, LOW);
  delayMicroseconds(2);

  digitalWrite(pinTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);

  long duration = pulseIn(pinEcho, HIGH, 30000);

  if (duration == 0) {
    return 999;
  }

  float hasil = duration * 0.0343 / 2.0;
  return (int)hasil;
}

// ================= FUNGSI STATUS TEKS =================
String getStatusText() {
  if (modeManual == 1) {
    return "MANUAL:BUANG";
  } 
  else if (modeManual == 2) {
    return "MANUAL:ISI";
  } 
  else if (modeManual == 3) {
    return "MANUAL:OFF";
  } 
  else {
    if (statusAir == 0) {
      return "AUTO:NORMAL";
    } 
    else if (statusAir == 1) {
      if (sedangJedaIsi) {
        return "AUTO:JEDA ISI";
      } else {
        return "AUTO:KURAS";
      }
    } 
    else if (statusAir == 2) {
      return "AUTO:ISI";
    }
  }

  return "UNKNOWN";
}

// ================= MODE AUTO =================
void jalankanModeAuto(int adcSekarang, int jarakSekarang) {
  // 1. NORMAL -> KURAS
  if (adcSekarang > batasKeruh && statusAir == 0) {
    statusAir = 1;
    sedangJedaIsi = false;

    pompaBuangOn();

    Serial.println("AUTO: Air keruh, pompa buang ON");
  }

  // 2. KURAS -> JEDA 2 DETIK -> ISI
  if (statusAir == 1) {
    if (jarakSekarang != 999 && jarakSekarang > BATAS_MULAI_ISI_CM && sedangJedaIsi == false) {
      semuaPompaOff();

      waktuMulaiJedaIsi = millis();
      sedangJedaIsi = true;

      Serial.println("AUTO: Air sudah rendah, pompa buang OFF, jeda sebelum isi");
    }

    if (sedangJedaIsi == true && millis() - waktuMulaiJedaIsi >= JEDA_SEBELUM_ISI_MS) {
      statusAir = 2;
      sedangJedaIsi = false;

      pompaIsiOn();

      Serial.println("AUTO: Jeda selesai, pompa isi ON");
    }
  }

  // 3. ISI -> NORMAL
  if (statusAir == 2) {
    if (jarakSekarang != 999 && jarakSekarang <= BATAS_STOP_ISI_CM) {
      statusAir = 0;
      sedangJedaIsi = false;

      semuaPompaOff();

      Serial.println("AUTO: Air cukup, semua pompa OFF, kembali NORMAL");
    }
  }
}

// ================= MODE MANUAL =================
void jalankanModeManual() {
  if (modeManual == 1) {
    pompaBuangOn();
  } 
  else if (modeManual == 2) {
    pompaIsiOn();
  } 
  else if (modeManual == 3) {
    semuaPompaOff();
  }
}

// ================= UPDATE SERIAL MONITOR =================
void updateSerialMonitor(int adcSekarang, int jarakSekarang) {
  if (millis() - waktuUpdateSerial >= INTERVAL_SERIAL_MS) {
    waktuUpdateSerial = millis();

    Serial.print("ADC: ");
    Serial.print(adcSekarang);

    Serial.print(" | Jarak: ");
    if (jarakSekarang == 999) {
      Serial.print("TIMEOUT");
    } else {
      Serial.print(jarakSekarang);
      Serial.print(" cm");
    }

    Serial.print(" | Status: ");
    Serial.print(getStatusText());

    Serial.print(" | Relay Buang: ");
    Serial.print(digitalRead(relayBuang) == POMPA_ON ? "ON" : "OFF");

    Serial.print(" | Relay Isi: ");
    Serial.println(digitalRead(relayIsi) == POMPA_ON ? "ON" : "OFF");
  }
}

// ================= TASK ADC =================
// Prioritas sedang.
// ADC hanya sebagai pemicu awal air keruh.
void TaskADC(void *pvParameters) {
  while (true) {
    nilaiADC = analogRead(pinPot);

    int nilaiPWM = map(nilaiADC, 0, 4095, 0, 255);
    analogWrite(ledKeruh, nilaiPWM);

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ================= TASK ULTRASONIK =================
// Prioritas lebih tinggi dari ADC.
// Sensor level air penting agar pompa tidak berlebihan.
void TaskUltrasonik(void *pvParameters) {
  while (true) {
    jarak = bacaJarak();

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ================= TASK KONTROL AIR =================
// Prioritas tertinggi.
// Mengontrol mode AUTO/MANUAL, ESP-NOW command, relay buang, dan relay isi.
void TaskKontrolAir(void *pvParameters) {
  while (true) {
    char cmd;

    // Proses command dari ESP kedua
    while (xQueueReceive(queueCommand, &cmd, 0) == pdTRUE) {
      if (cmd == 'R') {

        // Proteksi:
        // Kalau AUTO sedang KURAS / ISI, command manual diabaikan
        // supaya tidak tiba-tiba berubah menjadi MANUAL:BUANG.
        if (modeManual == 0 && statusAir != 0) {
          Serial.println("Command R diabaikan: AUTO sedang proses");
        } 
        else {
          modeManual++;

          if (modeManual > 3) {
            modeManual = 0;
          }

          statusAir = 0;
          sedangJedaIsi = false;
          semuaPompaOff();

          Serial.print("Mode manual berubah ke: ");
          Serial.println(getStatusText());
        }
      }
    }

    int adcSekarang = nilaiADC;
    int jarakSekarang = jarak;

    if (modeManual == 0) {
      jalankanModeAuto(adcSekarang, jarakSekarang);
    } else {
      jalankanModeManual();
    }

    updateSerialMonitor(adcSekarang, jarakSekarang);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // ================= ESP-NOW =================
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println();
  Serial.println("ESP UTAMA RTOS TANPA LCD");
  Serial.print("MAC ESP UTAMA: ");
  Serial.println(WiFi.macAddress());

  queueCommand = xQueueCreate(10, sizeof(char));

  if (queueCommand == NULL) {
    Serial.println("Queue command gagal dibuat!");
    return;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW gagal init!");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // ================= SETUP LED =================
  pinMode(ledBuang, OUTPUT);
  pinMode(ledIsi, OUTPUT);
  pinMode(ledKeruh, OUTPUT);

  digitalWrite(ledBuang, LOW);
  digitalWrite(ledIsi, LOW);
  digitalWrite(ledKeruh, LOW);

  // ================= SETUP RELAY =================
  pinMode(relayBuang, OUTPUT);
  pinMode(relayIsi, OUTPUT);

  semuaPompaOff();

  // ================= SETUP ULTRASONIK =================
  pinMode(pinTrig, OUTPUT);
  pinMode(pinEcho, INPUT);
  digitalWrite(pinTrig, LOW);

  delay(1000);

  // ================= BUAT TASK RTOS =================

  // Prioritas 2: Sensor kekeruhan
  xTaskCreatePinnedToCore(
    TaskADC,
    "TaskADC",
    2048,
    NULL,
    2,
    NULL,
    1
  );

  // Prioritas 3: Sensor level air
  xTaskCreatePinnedToCore(
    TaskUltrasonik,
    "TaskUltrasonik",
    3072,
    NULL,
    3,
    NULL,
    1
  );

  // Prioritas 4: Kontrol relay/pompa
  xTaskCreatePinnedToCore(
    TaskKontrolAir,
    "TaskKontrolAir",
    4096,
    NULL,
    4,
    NULL,
    1
  );

  Serial.println("ESP utama RTOS siap");
}

// ================= LOOP =================
void loop() {
  // Kosong karena proses utama berjalan di task RTOS
  vTaskDelay(pdMS_TO_TICKS(1000));
}