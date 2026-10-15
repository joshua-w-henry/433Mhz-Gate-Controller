#pragma once
#include <Arduino.h>
#include <SPI.h>
constexpr int CS=27, MISO_PIN=19;
bool radioOK=false;
uint8_t radioPart=255, radioVersion=255;
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

