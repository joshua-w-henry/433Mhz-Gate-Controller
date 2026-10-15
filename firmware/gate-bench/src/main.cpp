#include <Arduino.h>
#include <initializer_list>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// BENCH ONLY: no relay drive, no wireless commands, no Wi-Fi credentials.
constexpr int CS=27, MISO_PIN=19, CLOSED_PIN=32, BUTTON_PIN=13, POT_PIN=34;
constexpr float TRAVEL_DEG=90.0f; // Display scale only; change for actual hinge travel.
Adafruit_SSD1306 oled(128,64,&Wire,-1);
Preferences prefs;
bool displayOK=false, radioOK=false;
uint8_t radioPart=255, radioVersion=255, oledAddr=0;
int page=0, closedRaw=-1, openRaw=-1;
float filtered=0, position=0;
int motion=0; // -1 closing, 0 stationary, +1 opening
// Movement decisions use a timed displacement window, rather than adjacent ADC readings.
constexpr float MOTION_THRESHOLD_PCT=0.8f;
constexpr uint32_t MOTION_WINDOW_MS=350, MOTION_CONFIRM_MS=250, MOTION_STOP_MS=900;
float motionReference=0;
uint32_t motionWindowAt=0, candidateAt=0, lastMove=0;
int candidateMotion=0;
bool motionInitialized=false;
uint32_t lastSample=0, lastDraw=0, lastReport=0, samples=0;
String state="STARTING", notice="";
uint32_t noticeAt=0, activationCount=0;

struct Debounced {
  explicit Debounced(int p) : pin(p) {}
  int pin; bool raw=false, stable=false; uint32_t changed=0;
  void begin() { pinMode(pin,INPUT_PULLUP); raw=stable=(digitalRead(pin)==LOW); changed=millis(); }
  bool tick(uint32_t now) {
    bool v=digitalRead(pin)==LOW;
    if(v!=raw) { raw=v; changed=now; }
    if(stable!=raw && now-changed>=35) { stable=raw; return true; }
    return false;
  }
};
Debounced closedSwitch{CLOSED_PIN}, service{BUTTON_PIN};
bool buttonReady=false, armed=false, longShown=false;
uint8_t taps=0;
uint32_t downAt=0, armedAt=0;

bool calibrated() { return closedRaw>=0 && openRaw>=0 && abs(openRaw-closedRaw)>=400; }
void message(const String &s) { notice=s; noticeAt=millis(); Serial.println(s); }
void activateBench() {
  ++activationCount;
  Serial.printf("EVENT uptime_ms=%lu source=gate-service-button request=%lu action=SIMULATED_ONLY\n",
                (unsigned long)millis(),(unsigned long)activationCount);
  message("ACTUATION REQUESTED");
}
void tickButton(uint32_t now) {
  bool edge=service.tick(now);
  if(!buttonReady) { if(!service.stable && !service.raw) buttonReady=true; return; }
  if(armed && now-armedAt>=5000) { armed=false; taps=0; message("Sequence cancelled"); }
  if(edge && service.stable) { downAt=now; longShown=false; }
  if(service.stable && now-downAt>=3000 && !longShown) {
    longShown=true; message("Release to arm");
  }
  if(edge && !service.stable) {
    uint32_t held=now-downAt;
    if(held>=3000) { armed=true; armedAt=now; taps=0; message("ACTUATE: PRESS 3x"); }
    else if(armed) {
      if(held<600) {
        if(++taps==3) { armed=false; taps=0; activateBench(); }
        else message(String("Count ")+taps+"/3");
      } else { armed=false; taps=0; message("Sequence cancelled"); }
    } else { page=(page+1)%3; }
  }
}

bool readyRadio() {
  uint32_t t=micros();
  while(digitalRead(MISO_PIN)==HIGH) if(micros()-t>20000) return false;
  return true;
}
bool regRead(uint8_t addr,uint8_t &out,bool status=false) {
  SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
  digitalWrite(CS,LOW);
  bool ok=readyRadio();
  if(ok) { SPI.transfer(addr | (status?0xC0:0x80)); out=SPI.transfer(0); }
  digitalWrite(CS,HIGH); SPI.endTransaction(); return ok;
}
bool regWrite(uint8_t addr,uint8_t val) {
  SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0)); digitalWrite(CS,LOW);
  bool ok=readyRadio(); if(ok) { SPI.transfer(addr); SPI.transfer(val); }
  digitalWrite(CS,HIGH); SPI.endTransaction(); return ok;
}
void probeRadio() {
  radioOK=false;
  SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
  digitalWrite(CS,LOW); delayMicroseconds(10); digitalWrite(CS,HIGH); delayMicroseconds(50);
  digitalWrite(CS,LOW);
  bool ok=readyRadio();
  if(ok) { SPI.transfer(0x30); ok=readyRadio(); } // SRES, leaves radio idle
  digitalWrite(CS,HIGH); SPI.endTransaction();
  uint8_t original=0,check=0;
  if(ok) ok=regRead(0x30,radioPart,true) && regRead(0x31,radioVersion,true);
  // Writable register round-trip rejects disconnected/stuck MISO lines.
  if(ok) ok=regRead(0x09,original);
  if(ok) {
    ok=regWrite(0x09,0xA5) && regRead(0x09,check) && check==0xA5;
    bool restored=regWrite(0x09,original); ok=ok && restored;
  }
  radioOK=ok && radioPart==0 && radioVersion!=0 && radioVersion!=255;
  Serial.printf("CC1101 SPI %s PART=0x%02X VERSION=0x%02X (RF idle)\n",radioOK?"PASS":"FAIL",radioPart,radioVersion);
}

void resetMotion() {
  motion=0;
  candidateMotion=0;
  candidateAt=0;
  motionReference=position;
  motionWindowAt=millis();
  lastMove=millis();
  motionInitialized=false;
}

void tickMotion(uint32_t now) {
  if(!motionInitialized) {
    motionReference=position;
    motionWindowAt=lastMove=now;
    motionInitialized=true;
    return;
  }
  if(now-motionWindowAt < MOTION_WINDOW_MS) {
    if(motion!=0 && now-lastMove>=MOTION_STOP_MS) motion=0;
    return;
  }
  float displacement=position-motionReference;
  int observed=displacement>=MOTION_THRESHOLD_PCT?1:
               displacement<=-MOTION_THRESHOLD_PCT?-1:0;
  // A sufficiently large displacement keeps a moving state alive, but
  // a new/reversed direction must remain consistent for confirmation.
  if(observed!=0) {
    lastMove=now;
    if(observed==motion) {
      candidateMotion=0;
    } else if(observed!=candidateMotion) {
      candidateMotion=observed;
      candidateAt=now;
    } else if(now-candidateAt>=MOTION_CONFIRM_MS) {
      motion=observed;
      candidateMotion=0;
    }
  } else {
    candidateMotion=0;
  }
  motionReference=position;
  motionWindowAt=now;
  if(now-lastMove>=MOTION_STOP_MS) motion=0;
}

void tickSensors(uint32_t now) {
  closedSwitch.tick(now);
  if(now-lastSample<20) return;
  lastSample=now;
  int raw=analogRead(POT_PIN);
  filtered=samples==0?raw:filtered+0.18f*(raw-filtered); ++samples;
  if(calibrated()) {
    // Preserve the unrestricted reading for disagreement/fault detection.
    position=100.0f*(filtered-closedRaw)/(openRaw-closedRaw);
    tickMotion(now);
    if(position < -8 || position >108 || (closedSwitch.stable && fabsf(position)>5)) state="SENSOR DISAGREE";
    else if(closedSwitch.stable) state="CLOSED";
    else if(motion>0) state="OPENING";
    else if(motion<0) state="CLOSING";
    else if(fabsf(position-100)<=2) state="OPEN (ANGLE)";
    else state="STOPPED BETWEEN";
  } else {
    if(motionInitialized) resetMotion();
    state=closedSwitch.stable?"CLOSED":"UNCALIBRATED";
  }
  static String previous="";
  if(state!=previous) {
    Serial.printf("STATE uptime_ms=%lu %s\n",(unsigned long)now,state.c_str()); previous=state;
  }
}
void printStatus() {
  Serial.printf("raw=%.1f closedSwitch=%d calClosed=%d calOpen=%d calibrated=%d position=%.1f%% state=%s radio=%s requests=%lu\n",
    filtered,closedSwitch.stable,closedRaw,openRaw,calibrated(),calibrated()?constrain(position,0.0f,100.0f):0,
    state.c_str(),radioOK?"SPI_OK":"FAIL",(unsigned long)activationCount);
}
void command(char c) {
  if(c=='c' || c=='o') {
    if(samples<50) { message("Wait for ADC settle"); return; }
    if(c=='c' && !closedSwitch.stable) { message("Close toggle first"); return; }
    if(c=='o' && closedSwitch.stable) { message("Open toggle first"); return; }
    if(c=='c') { closedRaw=lroundf(filtered); prefs.putInt("closed",closedRaw); }
    else { openRaw=lroundf(filtered); prefs.putInt("open",openRaw); }
    resetMotion(); message(calibrated()?"Calibration ready":"Need both endpoints"); printStatus();
  } else if(c=='x') { prefs.clear(); closedRaw=openRaw=-1; resetMotion(); message("Calibration cleared"); }
  else if(c=='r') probeRadio();
  else if(c=='s') printStatus();
  else if(c=='h' || c=='?') Serial.println("c=save closed; o=save open; x=clear calibration; r=probe radio; s=status; h=help");
}
void draw(uint32_t now) {
  if(!displayOK || now-lastDraw<150) return;
  lastDraw=now; oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setCursor(0,0);
  oled.println("MM571 GATE / BENCH v1");
  if(armed) {
    oled.println("ACTUATE: PRESS 3x"); oled.printf("Count: %u/3\n",taps);
    oled.printf("Time: %lus\n",(unsigned long)((5000-(now-armedAt)+999)/1000));
    oled.println("Relay DISABLED");
  } else if(page==0) {
    oled.println(state);
    if(calibrated()) { oled.printf("Angle: %.1f deg\n",constrain(position,0.0f,100.0f)*TRAVEL_DEG/100); oled.printf("Travel: %.1f %%\n",position); }
    else { oled.println("Angle: NOT CALIBRATED"); oled.printf("Raw ADC: %.0f\n",filtered); }
    oled.printf("Closed input: %s\n",closedSwitch.stable?"ON":"OFF");
    oled.println("Power: USB / batt N/A");
  } else if(page==1) {
    oled.printf("CC1101 SPI: %s\n",radioOK?"PASS":"FAIL");
    oled.printf("Part:%02X Version:%02X\n",radioPart,radioVersion);
    oled.println("RF: idle (no packets)"); oled.println("RSSI: N/A"); oled.println("WiFi/ESP-NOW: off");
  } else {
    oled.printf("Raw ADC: %.0f\n",filtered); oled.printf("Closed cal: %d\n",closedRaw); oled.printf("Open cal: %d\n",openRaw);
    oled.printf("Requests: %lu\n",(unsigned long)activationCount); oled.println("Relay: DISABLED");
  }
  oled.setCursor(0,56);
  if(now-noticeAt<2500 && notice.length()) oled.print(notice);
  else oled.print("Tap:page Hold:service");
  oled.display();
}
void setup() {
  Serial.begin(115200);
  // GPIO33 is deliberately never configured or driven: relay stays disconnected.
  closedSwitch.begin(); service.begin();
  buttonReady=false;
  analogReadResolution(12); analogSetPinAttenuation(POT_PIN,ADC_11db);
  Wire.begin(21,22); Wire.setTimeOut(50);
  for(uint8_t addr: {uint8_t(0x3C),uint8_t(0x3D)}) {
    Wire.beginTransmission(addr);
    if(Wire.endTransmission()==0) { oledAddr=addr; break; }
  }
  if(oledAddr) displayOK=oled.begin(SSD1306_SWITCHCAPVCC,oledAddr,true,false);
  pinMode(CS,OUTPUT); digitalWrite(CS,HIGH); pinMode(MISO_PIN,INPUT_PULLUP);
  SPI.begin(18,19,23,CS);
  prefs.begin("gate-angle",false); closedRaw=prefs.getInt("closed",-1); openRaw=prefs.getInt("open",-1);
  Serial.println("\nMM571 GateNode Bench v1 - NO RELAY OUTPUT / NO RF COMMANDS");
  Serial.printf("OLED %s address=0x%02X\n",displayOK?"OK":"NOT FOUND",oledAddr);
  probeRadio(); command('h');
}
void loop() {
  uint32_t now=millis(); tickSensors(now); tickButton(now);
  while(Serial.available()) command(Serial.read());
  if(now-lastReport>=2000) { lastReport=now; printStatus(); }
  draw(now); delay(1);
}
