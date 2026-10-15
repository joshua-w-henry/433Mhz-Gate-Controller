#pragma once
#include <stdint.h>
// Active-low input passed as true when pressed. No hardware dependencies.
class Button {
public:
  void begin(bool pressed, uint32_t now) {
    raw=stable=pressed; changed=now; ready=!pressed;
  }
  bool update(bool pressed, uint32_t now) {
    if(pressed!=raw) { raw=pressed; changed=now; }
    if(now-changed<35) return false;
    if(!ready) { stable=raw; if(!raw) ready=true; return false; }
    if(raw==stable) return false;
    stable=raw;
    return stable; // One event on the debounced press edge; hold does not repeat.
  }
private:
  bool raw=false, stable=false, ready=false;
  uint32_t changed=0;
};
