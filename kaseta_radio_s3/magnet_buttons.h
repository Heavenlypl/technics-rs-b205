#pragma once
#include <stdint.h>
#include <stddef.h>
struct MagnetKey {const char *id,*name,*icon;int gpio;};
// Keep REW/FF on previously reserved GPIO14/15. Independent dry contacts to GND.
static constexpr MagnetKey MAGNET_KEYS[]={
 {"play","Play","mdi:play",16}, {"pause","Pause","mdi:pause",17},
 {"stop","Stop","mdi:stop",18}, {"prev","PREV","mdi:skip-previous",14},
 {"next","NEXT","mdi:skip-next",15}, {"pilot","Pilot","mdi:remote",21}
};
static constexpr size_t MAGNET_KEY_COUNT=sizeof(MAGNET_KEYS)/sizeof(MAGNET_KEYS[0]);
// Both edges debounced. Held-at-boot/reconnect keys must be released before firing.
class MagnetContact {
 bool raw=false,stable=false,armed=false; uint32_t changed=0;
public:
 void resync(bool down,uint32_t now){raw=stable=down;armed=!down;changed=now;}
 int update(bool down,uint32_t now){
  if(down!=raw){raw=down;changed=now;}
  if(raw==stable||uint32_t(now-changed)<25)return -1;
  stable=raw;
  if(!stable){bool wasArmed=armed;armed=true;return wasArmed?0:-1;}
  return armed?1:-1;
 }
};
