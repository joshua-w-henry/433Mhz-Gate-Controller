#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <initializer_list>
#include <esp_system.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "button.h"
#include "radio_probe.h"
#include <RadioLib.h>
#include "../../shared/radio_pins.h"

// Mobile bench commissioning only. No RF packets or physical gate activation.
Adafruit_SSD1306 oled(128,64,&Wire,-1);
Button activateButton, statusButton;
bool displayOK=false, screenOn=true;
uint8_t oledAddr=0;
uint32_t sessionID=0, sequence=0, lastInteraction=0, lastDraw=0, messageAt=0;
int page=0;
String banner="";
CC1101 packetRadio = new Module(RF_CS_PIN,RF_GDO0_PIN,RADIOLIB_NC,RF_GDO2_PIN);
volatile uint32_t gdo2Edges=0, gdo2HighEdges=0;
void IRAM_ATTR gdo2EdgeMonitor() {
  ++gdo2Edges;
  if(digitalRead(RF_GDO2_PIN)) ++gdo2HighEdges;
}
void beginGdo2Monitor() {
  pinMode(RF_GDO2_PIN,INPUT);
  attachInterrupt(digitalPinToInterrupt(RF_GDO2_PIN),gdo2EdgeMonitor,CHANGE);
}
void logGdo2Monitor(const char *site, uint32_t beforeEdges, uint32_t beforeHigh) {
  noInterrupts();
  uint32_t edges=gdo2Edges, high=gdo2HighEdges;
  interrupts();
  Serial.printf("RF_GDO2_TRACE site=%s edges=%lu rising=%lu level=%d\n",
    site,(unsigned long)(edges-beforeEdges),(unsigned long)(high-beforeHigh),digitalRead(RF_GDO2_PIN));
}

bool linkOK=false;
uint32_t benchPingSeq=0;
volatile bool mobileRxPending=false;
void IRAM_ATTR onMobileRadioPacket() { mobileRxPending=true; }
void wakeScreen();
void mobileRfDiagnostics(const char *site) {
  uint8_t marc=0,txBytes=0,rxBytes=0;
  bool ok=regRead(0x35,marc,true) && regRead(0x3A,txBytes,true) && regRead(0x3B,rxBytes,true);
  Serial.printf("RF_DIAG site=%s spi=%d GDO0=%d GDO2=%d MARCSTATE=0x%02X TXBYTES=0x%02X RXBYTES=0x%02X\n",
    site,ok,digitalRead(RF_GDO0_PIN),digitalRead(RF_GDO2_PIN),marc,txBytes,rxBytes);
}
void sendBenchPing() {
  if(!linkOK) { Serial.println("RF_BENCH link unavailable"); return; }
  String ping="BENCHPING:"+String(++benchPingSeq);
  uint32_t edgeBefore=gdo2Edges, highBefore=gdo2HighEdges;
  int sent=packetRadio.transmit(ping);
  if(sent==RADIOLIB_ERR_NONE) {
    mobileRxPending=false;
    packetRadio.setPacketReceivedAction(onMobileRadioPacket);
    int rxStart=packetRadio.startReceive();
    logGdo2Monitor("mobile_ping",edgeBefore,highBefore);
    Serial.printf("RF_TX payload=%s code=%d\n",ping.c_str(),sent);
    Serial.printf("RF_RX_ARM code=%d GDO0=%d\n",rxStart,digitalRead(RF_GDO0_PIN));
    bool gotReply=false;
    uint32_t waitStart=millis();
    while(rxStart==RADIOLIB_ERR_NONE && millis()-waitStart<1200) {
      if(mobileRxPending) {
        mobileRxPending=false;
        String reply;
        int rx=packetRadio.readData(reply);
        if(rx==RADIOLIB_ERR_NONE && reply=="BENCHACK:"+String(benchPingSeq)) {
          Serial.printf("RF_REPLY payload=%s rssi=%.1f\n",reply.c_str(),packetRadio.getRSSI());
          gotReply=true;
          break;
        }
        Serial.printf("RF_REPLY_IGNORED code=%d len=%u\n",rx,(unsigned)reply.length());
        packetRadio.startReceive();
      }
      delay(1);
    }
    packetRadio.standby();
    packetRadio.clearPacketReceivedAction();
    if(!gotReply) { Serial.println("RF_REPLY_TIMEOUT"); mobileRfDiagnostics("mobile_reply_rx_error"); }
  } else { logGdo2Monitor("mobile_ping",edgeBefore,highBefore); Serial.printf("RF_TX payload=%s code=%d\n",ping.c_str(),sent); mobileRfDiagnostics("mobile_ping_tx_error"); }
  wakeScreen();
  banner="RF PING TEST"; messageAt=millis();
}


void wakeScreen() {
  lastInteraction=millis();
  if(!screenOn && displayOK) oled.ssd1306_command(SSD1306_DISPLAYON);
  screenOn=true;
}
void requestBench(const char* action) {
  wakeScreen();
  ++sequence;
  // Session ID makes bench request IDs distinguishable across restarts.
  // This is NOT replay protection or authentication; no request is transmitted.
  Serial.printf("BENCH_REQUEST remote=\"%s\" session=%08lX seq=%lu action=%s delivery=NOT_SENT\n",
    REMOTE_NAME,(unsigned long)sessionID,(unsigned long)sequence,action);
  banner=String(action)+" TEST ONLY"; messageAt=millis();
}
void draw(uint32_t now) {
  if(!displayOK || !screenOn || now-lastDraw<150) return;
  lastDraw=now; oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1); oled.setCursor(0,0);
  oled.println(REMOTE_NAME);
  if(page==0) {
    oled.println("GATE: UNKNOWN");
    oled.println("Battery: NOT WIRED");
    oled.println("433: SPI test only");
    oled.printf("Radio chip: %s\n",radioOK?"PASS":"FAIL");
    oled.println("RSSI: --  no packets");
  } else {
    oled.println("MOBILE BENCH v0.1");
    oled.printf("OLED: 0x%02X\n",oledAddr);
    oled.printf("CC1101: %02X / %02X\n",radioPart,radioVersion);
    oled.printf("Requests: %lu\n",(unsigned long)sequence);
    oled.println("WiFi/BLE: disabled");
    oled.println("No gate connection");
  }
  oled.setCursor(0,56);
  if(banner.length() && now-messageAt<2500) oled.print(banner);
  else oled.print("A:ACT test S:STATUS");
  oled.display();
}
void setup() {
  Serial.begin(115200);
  pinMode(ACTIVATE_PIN,INPUT_PULLUP); pinMode(STATUS_PIN,INPUT_PULLUP);
  activateButton.begin(digitalRead(ACTIVATE_PIN)==LOW,millis());
  statusButton.begin(digitalRead(STATUS_PIN)==LOW,millis());
  sessionID=esp_random();
  Wire.begin(21,22); Wire.setTimeOut(50);
  for(uint8_t addr: {uint8_t(0x3C),uint8_t(0x3D)}) {
    Wire.beginTransmission(addr);
    if(Wire.endTransmission()==0) { oledAddr=addr; break; }
  }
  if(oledAddr) displayOK=oled.begin(SSD1306_SWITCHCAPVCC,oledAddr,true,false);
  pinMode(CS,OUTPUT); digitalWrite(CS,HIGH); pinMode(MISO_PIN,INPUT_PULLUP);
  SPI.begin(18,19,23,CS);
  Serial.printf("\nRF Gate Mobile BENCH v0.1 name=%s\n",REMOTE_NAME);
  Serial.printf("OLED %s address=0x%02X\n",displayOK?"OK":"NOT FOUND",oledAddr);
  probeRadio();
  int rc=packetRadio.begin(RF_FREQUENCY_MHZ);
  linkOK=(rc==RADIOLIB_ERR_NONE);
  beginGdo2Monitor();
  Serial.printf("RF_BENCH %s code=%d frequency=%.2fMHz\n",linkOK?"READY":"FAIL",rc,RF_FREQUENCY_MHZ);
  Serial.println("p=send harmless RF BENCHPING; no actuation commands transmitted");
  Serial.println("a=ACTIVATE test; s=STATUS test/page; r=radio probe; h=help");
  Serial.println("Requests are logged locally only. No RF transmissions. Gate remains UNKNOWN.");
  lastInteraction=millis();
}
void loop() {
  uint32_t now=millis();
  bool a=activateButton.update(digitalRead(ACTIVATE_PIN)==LOW,now);
  bool s=statusButton.update(digitalRead(STATUS_PIN)==LOW,now);
  // If both debounce on the same loop, STATUS wins. No combined gesture.
  if(s) { page=(page+1)%2; requestBench("STATUS"); }
  else if(a) requestBench("ACTIVATE");
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='p') sendBenchPing();
    else if(c=='a') requestBench("ACTIVATE");
    else if(c=='s') { page=(page+1)%2; requestBench("STATUS"); }
    else if(c=='r') { wakeScreen(); probeRadio(); }
    else if(c=='h') Serial.println("a=ACT test; s=STATUS test/page; r=radio probe. BENCH ONLY: no packets.");
  }
  now=millis();
  if(screenOn && now-lastInteraction>=SCREEN_IDLE_MS) {
    if(displayOK) oled.ssd1306_command(SSD1306_DISPLAYOFF);
    screenOn=false;
  }
  draw(now); delay(1);
}
