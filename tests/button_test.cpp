#include <cassert>
#include <iostream>
#include "../firmware/mobile/src/button.h"
int main() {
  Button b;
  b.begin(false,0);
  assert(!b.update(true,10)); assert(!b.update(true,44)); assert(b.update(true,45));
  assert(!b.update(true,5000));
  assert(!b.update(false,5010)); assert(!b.update(false,5045));
  assert(!b.update(true,5100)); assert(b.update(true,5135));
  b.begin(true,0); assert(!b.update(true,5000));
  assert(!b.update(false,5010)); assert(!b.update(false,5045));
  assert(!b.update(true,5100)); assert(b.update(true,5135));
  b.begin(false,0);
  for(uint32_t i=0;i<20;i++) assert(!b.update((i%2)==0,100+i*5));
  assert(!b.update(false,300));
  // Debounce time arithmetic must work across millis rollover.
  b.begin(false,0xFFFFFF00u); assert(!b.update(true,0xFFFFFFF0u));
  assert(b.update(true,0x00000014u));
  std::cout << "PASS: debounce, press once, release/repress, boot held, bounce, rollover\n";
}
