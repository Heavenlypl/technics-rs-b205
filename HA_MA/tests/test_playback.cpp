#include <cassert>
#include <initializer_list>
#include "../../kaseta_radio_s3/playback_state.h"
int main(){
 PlaybackMode m;assert(parsePlaybackCommand("play",4,m)&&m==PlaybackMode::Play);
 assert(parsePlaybackCommand("pause",5,m)&&m==PlaybackMode::Pause);
 assert(parsePlaybackCommand("stop",4,m)&&m==PlaybackMode::Stop);
 assert(!parsePlaybackCommand("Play",4,m));assert(!parsePlaybackCommand("playjunk",8,m));
 assert(!parsePlaybackCommand("play\0junk",9,m));
 for(auto mode:{PlaybackMode::Play,PlaybackMode::Pause,PlaybackMode::Stop}){
  bool healthy=playbackStreamHealthy(mode,3,1000,1100);
  assert(healthy==(mode!=PlaybackMode::Stop));
  assert(playbackReelsMoving(true,mode,healthy)==(mode==PlaybackMode::Play));
  assert(!playbackReelsMoving(false,mode,healthy));
  assert(!playbackStreamHealthy(mode,3,1000,1250));
  assert(!playbackStreamHealthy(mode,4,1000,1100));
 }
 assert(playbackStreamHealthy(PlaybackMode::Pause,3,0xfffffff0,0x20));
 assert(!playbackStreamHealthy(PlaybackMode::Play,3,0,100));
}
