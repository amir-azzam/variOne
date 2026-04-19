// CC1101 bring-up smoke test (raw SPI, no driver library).
// Build only with `pio run -e cc1101_test`. Safe to keep in src/ — the
// esp32dev env's build_src_filter excludes it from the main firmware.

#include <Arduino.h>
#include <SPI.h>

// Pin map — matches CLAUDE.md wiring table for E07-M1101D V2.0
#define CC_CSN   27
#define CC_GDO0  4
#define CC_SCK   18
#define CC_MISO  19
#define CC_MOSI  23

// Command strobes / status registers (see CC1101 datasheet §10.4, §29.3)
#define CC_SRES       0x30
#define CC_REG_PARTNUM 0x30
#define CC_REG_VERSION 0x31
#define READ_BURST    0xC0   // Status regs MUST be read with burst bit set,
                             // otherwise the header is interpreted as a strobe.

static SPISettings spiCfg(1000000, MSBFIRST, SPI_MODE0);

static void waitMisoLow(uint32_t timeout_ms = 50) {
  uint32_t t0 = millis();
  while (digitalRead(CC_MISO) == HIGH) {
    if (millis() - t0 > timeout_ms) return;
    delayMicroseconds(10);
  }
}

static uint8_t readStatusReg(uint8_t addr) {
  SPI.beginTransaction(spiCfg);
  digitalWrite(CC_CSN, LOW);
  waitMisoLow();
  SPI.transfer(addr | READ_BURST);
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(CC_CSN, HIGH);
  SPI.endTransaction();
  return v;
}

static void resetChip() {
  // Manual reset sequence per datasheet §19.1.2.
  digitalWrite(CC_CSN, HIGH); delayMicroseconds(5);
  digitalWrite(CC_CSN, LOW);  delayMicroseconds(10);
  digitalWrite(CC_CSN, HIGH); delayMicroseconds(45);
  SPI.beginTransaction(spiCfg);
  digitalWrite(CC_CSN, LOW);
  waitMisoLow();
  SPI.transfer(CC_SRES);
  waitMisoLow();
  digitalWrite(CC_CSN, HIGH);
  SPI.endTransaction();
  delay(10);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== variOne CC1101 smoke test ==="));
  Serial.println(F("Expect: PARTNUM=0x00, VERSION=0x04 or 0x14"));

  // CRITICAL: CSN must be driven HIGH before SPI.begin(), otherwise the
  // CC1101 can latch onto boot-time SPI traffic and corrupt the bus.
  pinMode(CC_CSN, OUTPUT);
  digitalWrite(CC_CSN, HIGH);
  pinMode(CC_GDO0, INPUT);
  delay(10);

  SPI.begin(CC_SCK, CC_MISO, CC_MOSI, CC_CSN);
  resetChip();
}

void loop() {
  uint8_t partnum = readStatusReg(CC_REG_PARTNUM);
  uint8_t version = readStatusReg(CC_REG_VERSION);

  bool ok = (partnum == 0x00) && (version == 0x04 || version == 0x14);
  Serial.printf("PARTNUM=0x%02X  VERSION=0x%02X  -> %s\n",
                partnum, version, ok ? "OK" : "FAIL");

  if (!ok) {
    if (partnum == 0xFF && version == 0xFF) {
      Serial.println(F("  hint: all 0xFF -> MISO floating / chip unpowered / bad VCC"));
    } else if (partnum == 0x00 && version == 0x00) {
      Serial.println(F("  hint: all 0x00 -> MISO stuck low, wrong MISO pin, or CSN not toggling"));
    } else {
      Serial.println(F("  hint: noisy reply -> MOSI/MISO swap, loose jumper, or clock too fast"));
    }
  }
  delay(2000);
}
