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
constexpr float ANGLE_TOLERANCE_DEG=5.0f;
constexpr float OPEN_ENTER_PCT=100.0f*(1.0f-ANGLE_TOLERANCE_DEG/TRAVEL_DEG);
constexpr float OPEN_EXIT_PCT=100.0f*(1.0f-2.0f*ANGLE_TOLERANCE_DEG/TRAVEL_DEG);
constexpr uint32_t SETTLE_MS=1500, AGREEMENT_GRACE_MS=2000;
bool openAngle=false, settled=false, mismatchPending=false, sensorFault=false;
uint32_t settledSince=0, mismatchSince=0;
int motion=0; // -1 closing, 0 stationary, +1 opening
// Movement decisions use a timed displacement window, rather than adjacent ADC readings.
constexpr float MOTION_THRESHOLD_PCT=0.8f;
constexpr uint32_t MOTION_WINDOW_MS=350, MOTION_CONFIRM_MS=700, MOTION_STOP_MS=900;
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
// Operational fault is latched for the session until explicitly cleared.
// Keep raw sensor health separate and never clear an active sensor problem.
String operationFault="NONE";
float faultAngle=0, faultPosition=0;
bool faultClosed=false;
uint32_t faultAt=0;
bool faultAcknowledged=false;
bool faultActive() { return sensorFault || operationFault!="NONE"; }
void latchOperationFault(const char *name, uint32_t now) {
  if(operationFault=="NONE") {
    operationFault=name; faultAt=now;
    faultAngle=calibrated()?position*TRAVEL_DEG/100.0f:0;
    faultPosition=constrain(position,0.0f,100.0f);
    faultClosed=closedSwitch.stable;
    faultAcknowledged=false;
    Serial.printf("FAULT_LATCHED type=%s angle_deg=%.1f position_pct=%.1f closed_switch=%d uptime_ms=%lu\n",
      name,faultAngle,faultPosition,faultClosed,(unsigned long)now);
  }
}


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
  settled=false; settledSince=0;
  mismatchPending=false; mismatchSince=0;
  sensorFault=false; openAngle=false;
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
  bool closedEdge=closedSwitch.tick(now);
  if(now-lastSample<20) return;
  lastSample=now;
  int raw=analogRead(POT_PIN);
  filtered=samples==0?raw:filtered+0.18f*(raw-filtered); ++samples;
  if(calibrated()) {
    position=100.0f*(filtered-closedRaw)/(openRaw-closedRaw);
    tickMotion(now);
    float angle=position*TRAVEL_DEG/100.0f;
    if(openAngle) {
      if(position<OPEN_EXIT_PCT) openAngle=false;
    } else if(position>=OPEN_ENTER_PCT) openAngle=true;

    // Settled agreement: 5-degree spatial tolerance plus temporal grace.
    if(closedEdge || motion!=0) {
      settled=false; settledSince=0;
      mismatchPending=false; mismatchSince=0; sensorFault=false;
    }
    if(motion==0 && settledSince==0) settledSince=now;
    settled=motion==0 && now-settledSince>=SETTLE_MS;
    bool nearClosed=fabsf(angle)<=ANGLE_TOLERANCE_DEG;
    bool invalidRange=angle < -ANGLE_TOLERANCE_DEG ||
                      angle > TRAVEL_DEG+ANGLE_TOLERANCE_DEG;
    bool disagrees=invalidRange || (closedSwitch.stable && !nearClosed);
    if(settled && disagrees) {
      if(!mismatchPending) { mismatchPending=true; mismatchSince=now; }
      if(now-mismatchSince>=AGREEMENT_GRACE_MS) sensorFault=true;
    } else {
      mismatchPending=false; mismatchSince=0; sensorFault=false;
    }

    if(sensorFault) state="SENSOR DISAGREE";
    else if(closedSwitch.stable && !disagrees) state="CLOSED";
    else if(motion>0) state="OPENING";
    else if(motion<0) state="CLOSING";
    else if(settled && openAngle) state="OPEN (ANGLE)";
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
// Observation-only movement expectation supervisor. No relay or RF actuation.
constexpr uint32_t START_TIMEOUT_MS=5000, STALL_TIMEOUT_MS=5000, TRAVEL_TIMEOUT_MS=45000;
// Require at least ~5 degrees of confirmed travel before classifying a stall.
constexpr float START_DELTA_PCT=100.0f*ANGLE_TOLERANCE_DEG/TRAVEL_DEG;
constexpr float REVERSE_DELTA_PCT=START_DELTA_PCT, PROGRESS_DELTA_PCT=0.8f;
struct MovementExpectation {
  bool active=false, started=false;
  int direction=0;  // +1 expects opening, -1 expects closing
  uint32_t requestedAt=0, lastProgressAt=0;
  float initialPct=0, progressPct=0;
} expected;

void publishMovementEvent(const char *eventName, uint32_t now) {
  const float angle=calibrated()?position*TRAVEL_DEG/100.0f:0.0f;
  Serial.printf(
    "EVENT {\"type\":\"%s\",\"uptime_ms\":%lu,\"angle_deg\":%.1f,"
    "\"position_pct\":%.1f,\"closed_switch\":%s,\"open_switch\":null,"
    "\"expected_direction\":\"%s\",\"elapsed_ms\":%lu,"
    "\"settled\":%s,\"sensor_fault\":%s,\"source\":\"bench_simulation\"}\n",
    eventName,(unsigned long)now,angle,constrain(position,0.0f,100.0f),
    closedSwitch.stable?"true":"false",
    expected.direction>0?"OPENING":expected.direction<0?"CLOSING":"NONE",
    (unsigned long)(now-expected.requestedAt),
    settled?"true":"false",sensorFault?"true":"false");
}
void startExpectation(int direction) {
  uint32_t now=millis();
  if(!calibrated() || faultActive()) {
    message("Cannot simulate: calibration/fault");
    return;
  }
  expected={};
  expected.active=true;
  expected.direction=direction;
  expected.requestedAt=expected.lastProgressAt=now;
  expected.initialPct=expected.progressPct=position;
  Serial.printf("EXPECT uptime_ms=%lu direction=%s output=DISABLED\n",
    (unsigned long)now,direction>0?"OPENING":"CLOSING");
}
void tickExpectation(uint32_t now) {
  if(!expected.active || !calibrated()) return;
  // Faults already qualified by the independent sensor subsystem.
  if(sensorFault) {
    publishMovementEvent("SENSOR_DISAGREE",now);
    expected.active=false;
    return;
  }
  bool reached=expected.direction>0 ?
    settled && openAngle && !closedSwitch.stable :
    settled && closedSwitch.stable && fabsf(position*TRAVEL_DEG/100.0f)<=ANGLE_TOLERANCE_DEG;
  if(reached) {
    Serial.printf("EXPECT_COMPLETE uptime_ms=%lu direction=%s elapsed_ms=%lu\n",
      (unsigned long)now,expected.direction>0?"OPENING":"CLOSING",
      (unsigned long)(now-expected.requestedAt));
    expected.active=false;
    return;
  }
  float signedFromStart=(position-expected.initialPct)*expected.direction;
  float signedProgress=(position-expected.progressPct)*expected.direction;
  if(signedFromStart<=-REVERSE_DELTA_PCT) {
    publishMovementEvent("WRONG_DIRECTION",now);
    latchOperationFault("WRONG_DIRECTION",now);
    expected.active=false;
    return;
  }
  // A closed switch still asserted rules out confirmed opening travel.
  // Noise near either endpoint must not turn NO_MOVEMENT into STALLED.
  bool startConfirmed=signedFromStart>=START_DELTA_PCT &&
    (expected.direction<0 || !closedSwitch.stable);
  if(!expected.started && startConfirmed) {
    expected.started=true;
    expected.progressPct=position;
    expected.lastProgressAt=now;
    Serial.printf("EXPECT_STARTED uptime_ms=%lu angle_deg=%.1f closed_switch=%d\n",
      (unsigned long)now,position*TRAVEL_DEG/100.0f,closedSwitch.stable);
  }
  if(expected.started && signedProgress>=PROGRESS_DELTA_PCT) {
    expected.progressPct=position;
    expected.lastProgressAt=now;
  }
  if(!expected.started && now-expected.requestedAt>=START_TIMEOUT_MS) {
    publishMovementEvent("NO_MOVEMENT",now);
    latchOperationFault("NO_MOVEMENT",now);
    expected.active=false;
  } else if(expected.started && now-expected.lastProgressAt>=STALL_TIMEOUT_MS) {
    const char *fault=expected.direction>0?"STALLED_OPENING":"STALLED_CLOSING";
    publishMovementEvent(fault,now);
    latchOperationFault(fault,now);
    expected.active=false;
  } else if(now-expected.requestedAt>=TRAVEL_TIMEOUT_MS) {
    publishMovementEvent("TRAVEL_TIMEOUT",now);
    latchOperationFault("TRAVEL_TIMEOUT",now);
    expected.active=false;
  }
}

void printStatus() {
  Serial.printf("raw=%.1f closedSwitch=%d calClosed=%d calOpen=%d calibrated=%d position=%.1f%% state=%s radio=%s requests=%lu settled=%d mismatchPending=%d sensorFault=%d operationFault=%s faultActive=%d acknowledged=%d\n",
    filtered,closedSwitch.stable,closedRaw,openRaw,calibrated(),calibrated()?constrain(position,0.0f,100.0f):0,
    state.c_str(),radioOK?"SPI_OK":"FAIL",(unsigned long)activationCount,settled,mismatchPending,sensorFault,operationFault.c_str(),faultActive(),faultAcknowledged);
}
void command(char c) {
  if(c=='A') {
    if(!faultActive()) { message("No fault to acknowledge"); return; }
    faultAcknowledged=true;
    message("Fault acknowledged (still active)");
    return;
  }
  if(c=='F') {
    if(expected.active || sensorFault || mismatchPending) {
      message("Cannot clear: active expectation or sensor problem");
      return;
    }
    operationFault="NONE"; faultAcknowledged=false;
    message("Operational fault cleared");
    return;
  }
  if(c=='O' || c=='C') { startExpectation(c=='O'?1:-1); return; }
  if(c=='K') { expected.active=false; message("Expectation cancelled"); return; }
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
  else if(c=='h' || c=='?') Serial.println("c=save closed; o=save open; x=clear calibration; r=probe radio; s=status; O=expect open; C=expect close; K=cancel; A=ack; F=clear fault; h=help");
}
void draw(uint32_t now) {
  if(!displayOK || now-lastDraw<150) return;
  lastDraw=now; oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setCursor(0,0);
  oled.println("MM571 GATE / BENCH v1");
  if(faultActive()) {
    oled.println("!! GATE FAULT !!");
    oled.println(sensorFault?"SENSOR DISAGREE":operationFault);
    oled.printf("Now: %.1f deg\n",constrain(position,0.0f,100.0f)*TRAVEL_DEG/100.0f);
    if(operationFault!="NONE") {
      oled.printf("At fault: %.1f deg\n",faultAngle);
      oled.printf("Closed: %s\n",faultClosed?"YES":"NO");
    }
    oled.println(faultAcknowledged?"ACK / STILL ACTIVE":"A=ACK  F=CLEAR");
  } else if(armed) {
    oled.println("ACTUATE: PRESS 3x"); oled.printf("Count: %u/3\n",taps);
    oled.printf("Time: %lus\n",(unsigned long)((5000-(now-armedAt)+999)/1000));
    oled.println("Relay DISABLED");
  } else if(page==0) {
    oled.println(state);
    if(calibrated()) { oled.printf("Angle: %.1f deg\n",constrain(position,0.0f,100.0f)*TRAVEL_DEG/100); oled.printf("Travel: %.1f %%\n",constrain(position,0.0f,100.0f)); }
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
  uint32_t now=millis(); tickSensors(now); tickButton(now); tickExpectation(now);
  while(Serial.available()) command(Serial.read());
  if(now-lastReport>=2000) { lastReport=now; printStatus(); }
  draw(now); delay(1);
}
