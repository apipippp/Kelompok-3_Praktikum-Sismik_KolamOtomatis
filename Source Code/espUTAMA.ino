#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>

// ================= LCD 16x2 =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= PIN ESP32 UTAMA =================
const int pinPot = 34;

const int ledBuang = 26;
const int ledIsi = 27;
const int ledKeruh = 14;

// Relay 2 channel
const int relayBuang = 19;
const int relayIsi   = 23;

const int pinTrig = 32;
const int pinEcho = 33;

// ================= SETTING RELAY NO =================
// Kebanyakan relay module aktif LOW
const int RELAY_AKTIF = LOW;
const int RELAY_MATI  = HIGH;

const int POMPA_ON  = RELAY_AKTIF;
const int POMPA_OFF = RELAY_MATI;

// ================= DATA ESP-NOW =================
typedef struct pesan_t {
  char command;
} pesan_t;

pesan_t dataMasuk;

volatile bool triggerModeRelay = false;

// ================= PARAMETER AIR =================
int batasKeruh = 2000;
int statusAir = 0;
// 0 = NORMAL
// 1 = KURAS
// 2 = ISI

// ================= MODE MANUAL =================
// 0 = AUTO
// 1 = MANUAL BUANG
// 2 = MANUAL ISI
// 3 = MANUAL OFF
int modeManual = 0;

// ================= ULTRASONIK PAKAI MILLIS =================
int jarak = 999;
unsigned long waktuBacaUltrasonik = 0;
const unsigned long INTERVAL_ULTRASONIK_MS = 500;

// ================= JEDA KURAS KE ISI PAKAI MILLIS =================
unsigned long waktuMulaiJedaIsi = 0;
const unsigned long JEDA_SEBELUM_ISI_MS = 2000;
bool sedangJedaIsi = false;

// ================= LCD UPDATE =================
unsigned long waktuUpdateLCD = 0;
const unsigned long INTERVAL_LCD_MS = 500;

// ================= CALLBACK ESP-NOW =================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&dataMasuk, incomingData, sizeof(dataMasuk));

  if (dataMasuk.command == 'R') {
    triggerModeRelay = true;
  }
}
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&dataMasuk, incomingData, sizeof(dataMasuk));

  if (dataMasuk.command == 'R') {
    triggerModeRelay = true;
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

  float hasil = duration * 0.0343 / 2;
  return (int)hasil;
}

// ================= UPDATE ULTRASONIK =================
void updateUltrasonik() {
  if (millis() - waktuBacaUltrasonik >= INTERVAL_ULTRASONIK_MS) {
    waktuBacaUltrasonik = millis();
    jarak = bacaJarak();
  }
}

// ================= UPDATE MODE DARI ESP-NOW =================
void updateModeRelayESPNow() {
  if (triggerModeRelay) {
    triggerModeRelay = false;

    modeManual++;

    if (modeManual > 3) {
      modeManual = 0;
    }

    statusAir = 0;
    sedangJedaIsi = false;
    semuaPompaOff();

    Serial.print("Mode sekarang: ");
    Serial.println(modeManual);
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

// ================= MODE AUTO =================
void jalankanModeAuto(int nilaiADC) {
  // 1. NORMAL -> KURAS
  if (nilaiADC > batasKeruh && statusAir == 0) {
    statusAir = 1;
    sedangJedaIsi = false;

    pompaBuangOn();
  }

  // 2. KURAS -> JEDA 2 DETIK -> ISI
  if (statusAir == 1) {
    if (jarak != 999 && jarak > 20 && sedangJedaIsi == false) {
      semuaPompaOff();

      waktuMulaiJedaIsi = millis();
      sedangJedaIsi = true;
    }

    if (sedangJedaIsi == true && millis() - waktuMulaiJedaIsi >= JEDA_SEBELUM_ISI_MS) {
      statusAir = 2;
      sedangJedaIsi = false;

      pompaIsiOn();
    }
  }

  // 3. ISI -> NORMAL
  if (statusAir == 2) {
    if (jarak != 999 && jarak <= 8) {
      statusAir = 0;
      sedangJedaIsi = false;

      semuaPompaOff();
    }
  }
}

// ================= UPDATE LCD =================
void updateLCD(int nilaiADC) {
  if (millis() - waktuUpdateLCD >= INTERVAL_LCD_MS) {
    waktuUpdateLCD = millis();

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Air:");
    lcd.print(jarak);
    lcd.print("cm ");

    lcd.print("A:");
    lcd.print(nilaiADC);

    lcd.setCursor(0, 1);

    if (modeManual == 1) {
      lcd.print("MANUAL:BUANG   ");
    } 
    else if (modeManual == 2) {
      lcd.print("MANUAL:ISI     ");
    } 
    else if (modeManual == 3) {
      lcd.print("MANUAL:OFF     ");
    } 
    else {
      if (statusAir == 0) {
        lcd.print("AUTO:NORMAL    ");
      } 
      else if (statusAir == 1) {
        if (sedangJedaIsi) {
          lcd.print("AUTO:JEDA ISI  ");
        } else {
          lcd.print("AUTO:KURAS     ");
        }
      } 
      else if (statusAir == 2) {
        lcd.print("AUTO:ISI       ");
      }
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println();
  Serial.print("MAC ESP UTAMA: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW gagal init!");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // I2C LCD
  Wire.begin(21, 22);
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("ESP UTAMA");
  lcd.setCursor(0, 1);
  lcd.print("ESP-NOW RX");

  // Setup LED
  pinMode(ledBuang, OUTPUT);
  pinMode(ledIsi, OUTPUT);
  pinMode(ledKeruh, OUTPUT);

  digitalWrite(ledBuang, LOW);
  digitalWrite(ledIsi, LOW);
  digitalWrite(ledKeruh, LOW);

  // Setup Relay
  pinMode(relayBuang, OUTPUT);
  pinMode(relayIsi, OUTPUT);

  semuaPompaOff();

  // Setup Ultrasonik
  pinMode(pinTrig, OUTPUT);
  pinMode(pinEcho, INPUT);
  digitalWrite(pinTrig, LOW);

  delay(1500);
  lcd.clear();
}

// ================= LOOP =================
void loop() {
  int nilaiADC = analogRead(pinPot);

  int nilaiPWM = map(nilaiADC, 0, 4095, 0, 255);
  analogWrite(ledKeruh, nilaiPWM);

  updateUltrasonik();

  updateModeRelayESPNow();

  if (modeManual == 0) {
    jalankanModeAuto(nilaiADC);
  } else {
    jalankanModeManual();
  }

  updateLCD(nilaiADC);
}