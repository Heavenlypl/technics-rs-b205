#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum class PlaybackMode:uint8_t {Play,Pause,Stop};
inline bool parsePlaybackCommand(const char *data,size_t size,PlaybackMode &mode){
 if(size==4&&!memcmp(data,"play",4))mode=PlaybackMode::Play;
 else if(size==5&&!memcmp(data,"pause",5))mode=PlaybackMode::Pause;
 else if(size==4&&!memcmp(data,"stop",4))mode=PlaybackMode::Stop;
 else return false;
 return true;
}
inline bool playbackStreamHealthy(PlaybackMode mode,uint32_t state,uint32_t last,uint32_t now){
 return mode!=PlaybackMode::Stop&&state==3&&last&&uint32_t(now-last)<250;
}
inline bool playbackReelsMoving(bool animationEnabled,PlaybackMode mode,bool healthy){
 return animationEnabled&&mode==PlaybackMode::Play&&healthy;
}
