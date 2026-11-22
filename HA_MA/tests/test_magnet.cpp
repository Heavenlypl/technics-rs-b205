#include <cassert>
#include <cstring>
#include <cstdio>
#include "../../kaseta_radio_s3/magnet_mqtt_protocol.h"
int main(){
 for(size_t i=0;i<MAGNET_KEY_COUNT;i++){
  MagnetContact b;b.resync(false,0);
  assert(b.update(true,1)==-1);assert(b.update(false,10)==-1);
  assert(b.update(true,15)==-1);assert(b.update(true,39)==-1);
  assert(b.update(true,40)==1);assert(b.update(true,2000)==-1);
  assert(b.update(false,2001)==-1);assert(b.update(false,2026)==0);
  assert(b.update(true,2030)==-1);assert(b.update(true,2055)==1);
  b.resync(true,3000);assert(b.update(true,4000)==-1);
  b.update(false,4001);assert(b.update(false,4026)==-1);
  b.update(true,4030);assert(b.update(true,4055)==1);
  b.resync(false,0xfffffff0);b.update(true,0xfffffff1);
  assert(b.update(true,0x20)==1);
  char topic[128],json[1024];assert(magnetDiscovery(i,"025445535401",topic,sizeof(topic),json,sizeof(json)));
  printf("%s\t%s\n",topic,json);
  char tiny[5];assert(!magnetDiscovery(i,"025445535401",tiny,sizeof(tiny),json,sizeof(json)));
 }
 assert(MAGNET_KEYS[3].gpio==14&&MAGNET_KEYS[4].gpio==15);
 for(size_t i=0;i<MAGNET_KEY_COUNT;i++)for(size_t j=i+1;j<MAGNET_KEY_COUNT;j++)assert(MAGNET_KEYS[i].gpio!=MAGNET_KEYS[j].gpio);
}
