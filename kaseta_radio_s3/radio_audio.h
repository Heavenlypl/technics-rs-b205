#pragma once
#include <WiFi.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <new>
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorFLAC.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "network_private.h"
#include "board_pins.h"
#include "playback_state.h"

std::atomic<uint32_t> audioLastMs{0}, audioSamples{0}, audioBitrate{0};
std::atomic<uint32_t> audioRate{0}, audioFill{0}, audioUnderflows{0};
std::atomic<uint32_t> audioConnections{0}, audioState{0}, audioStackFree{0};
std::atomic<PlaybackMode> audioMode{PlaybackMode::Stop};
std::atomic<uint32_t> audioVolume{100};
std::atomic<bool> audioMute{false};
static portMUX_TYPE audioUriMux=portMUX_INITIALIZER_UNLOCKED;
static char audioUri[1536]={0};
static std::atomic<bool> audioUriReady{false};
enum class AudioCodec : uint8_t { MP3, FLAC };
static AudioCodec requestedCodec=AudioCodec::MP3;
static std::atomic<AudioCodec> audioCodec{AudioCodec::MP3};
static std::atomic<uint32_t> audioBufferCapacity{0};

std::atomic<uint32_t> audioStopEpoch{0},audioMutedSamples{0};
static constexpr uint32_t tapeSeekDurationMs=2000;
static std::atomic<uint32_t> tapeSeekStarted{0},tapeSeekOldSession{0};

static void beginTapeSeek(bool forward) {
  tapeSeekOldSession.store(audioStopEpoch.load(),std::memory_order_relaxed);
  // The low bit records direction. Keep the timestamp and direction atomic.
  tapeSeekStarted.store((millis()&~1UL)|(forward?1UL:0UL));
}

static void cancelTapeSeek() { tapeSeekStarted.store(0); }

static int tapeSeekDirection() {
  const uint32_t started=tapeSeekStarted.load();
  if(!started||uint32_t(millis()-(started&~1UL))>=tapeSeekDurationMs)return 0;
  return (started&1U)?1:-1;
}

static bool tapeSeekMutesAudio() {
  uint32_t started=tapeSeekStarted.load();
  if(!started)return false;
  const uint32_t elapsed=millis()-(started&~1UL);
  if(elapsed<tapeSeekDurationMs)return true;
  // Do not let the old station leak back if MA needs longer to switch.
  // Recover the current station if a selection fails and leaves it running.
  if(elapsed<6000&&audioStopEpoch.load()==tapeSeekOldSession.load(std::memory_order_relaxed))return true;
  tapeSeekStarted.compare_exchange_strong(started,0);
  return false;
}

bool radioSetURI(const char *uri,AudioCodec codec){
 const size_t n=strlen(uri);
 if(!n||n>=sizeof(audioUri)||strncmp(uri,"http://",7)||strchr(uri,'\r')||strchr(uri,'\n'))return false;
 audioMode.store(PlaybackMode::Stop);audioStopEpoch.fetch_add(1);
 portENTER_CRITICAL(&audioUriMux);
 memcpy(audioUri,uri,n+1);
 requestedCodec=codec;
 audioUriReady.store(true);
 portEXIT_CRITICAL(&audioUriMux);
 return true;
}

TaskHandle_t audioTaskHandle=nullptr;

bool radioRequestMode(PlaybackMode requested){
 if(requested==PlaybackMode::Pause){
  PlaybackMode expected=PlaybackMode::Play;
  return audioMode.compare_exchange_strong(expected,PlaybackMode::Pause);
 }
 if(requested==PlaybackMode::Stop)audioStopEpoch.fetch_add(1);
 audioMode.store(requested);return true;
}
// Keep the running decoder and I2S clock active while the output is muted.
void radioTogglePause(){
 PlaybackMode mode=audioMode.load();
 while(mode!=PlaybackMode::Stop){
  const PlaybackMode next=mode==PlaybackMode::Pause?PlaybackMode::Play:PlaybackMode::Pause;
  if(audioMode.compare_exchange_weak(mode,next))return;
 }
}

void radioResumePaused(){
 PlaybackMode expected=PlaybackMode::Pause;
 audioMode.compare_exchange_strong(expected,PlaybackMode::Play);
}

alignas(4) uint8_t fallbackAudioBuffer[16384];
static uint8_t *networkAudioBuffer=fallbackAudioBuffer;
static size_t networkAudioBufferSize=sizeof(fallbackAudioBuffer);
static constexpr size_t mp3BufferCapacity=64*1024;
static constexpr size_t flacBufferCapacity=512*1024;

class CooperativeRadioSource : public AudioFileSourceICYStream {
  const uint32_t epoch;
  const bool flac;
  bool initialRead=true;
public:
  CooperativeRadioSource(uint32_t sessionEpoch,bool lossless):epoch(sessionEpoch),flac(lossless){}

  uint32_t read(void *data,uint32_t length) override {
    if(!length)return 0;
    const bool prefill=length>sizeof(fallbackAudioBuffer);
    const bool startingFlac=flac&&prefill&&initialRead;
    initialRead=false;
    // Start with a smaller reserve. The ring buffer still grows to its full
    // capacity as playback continues, and underflow recovery uses the full prefill.
    if(startingFlac)length=min<uint32_t>(length,128U*1024U);
    const uint32_t timeout=startingFlac?2200:(flac?(prefill?5500:1200):(prefill?1900:500));
    uint32_t started=millis(),received=0;
    auto *bytes=static_cast<uint8_t *>(data);
    do {
      if(audioMode.load()==PlaybackMode::Stop||epoch!=audioStopEpoch.load())return received;
      received+=readNonBlock(bytes+received,length-received);
      if(received==length||!isOpen())return received;
      // yield() in the stock blocking reader does not let lower-priority tasks run.
      vTaskDelay(1);
    }while(millis()-started<timeout);
    return received;
  }
};

class SessionAudioBuffer : public AudioFileSourceBuffer {
  const uint32_t epoch;
  uint32_t lastPause=millis();
  uint32_t bytesDelivered=0;
public:
  SessionAudioBuffer(AudioFileSource *source,uint32_t sessionEpoch,size_t capacity)
   :AudioFileSourceBuffer(source,networkAudioBuffer,capacity),epoch(sessionEpoch){}

  uint32_t consumedBytes() const {return bytesDelivered;}

  uint32_t read(void *data,uint32_t length) override {
    if(audioMode.load()==PlaybackMode::Stop||epoch!=audioStopEpoch.load())return 0;
    // The decoder can search for an MP3 header within one loop() call.
    if(millis()-lastPause>=20){vTaskDelay(1);lastPause=millis();}
    const uint32_t count=AudioFileSourceBuffer::read(data,length);
    bytesDelivered+=count;
    return count;
  }
};

class MonitoredI2S : public AudioOutputI2S {
  uint32_t accepted=0,muted=0;
public:
  bool ConsumeSample(int16_t sample[2]) override {
    // Keep decoding/draining live radio during Pause, but submit silence to DAC.
    // This preserves the connection and stream-health heartbeat independently of sound.
    bool silent=audioMode.load()!=PlaybackMode::Play||audioMute.load()||tapeSeekMutesAudio();
    int16_t zero[2]={0,0};
    bool ok=AudioOutputI2S::ConsumeSample(silent?zero:sample);
    if(ok&&silent&&((++muted&255)==0))audioMutedSamples.store(muted,std::memory_order_relaxed);
    if(ok && ((++accepted & 255)==0)) {
      audioSamples.store(accepted,std::memory_order_relaxed);
      audioLastMs.store(millis(),std::memory_order_relaxed);
    }
    return ok;
  }
};

class MonitoredMP3 : public AudioGeneratorMP3 {
public:
  uint32_t bitrate() const {return frame?frame->header.bitrate/1000:0;}
  uint32_t sampleRate() const {return lastRate;}
};

class MonitoredFLAC : public AudioGeneratorFLAC {
public:
  uint32_t rate() const {return sampleRate;}
};

void sourceStatus(void *,int code,const char *message) {
  // Ten callback podłączony jest tylko do źródła HTTP.
  if(code!=0) Serial.printf("HTTP status %d: %s\n",code,message);
}
void bufferStatus(void *,int code,const char *) {
  if(code==AudioFileSourceBuffer::STATUS_UNDERFLOW)
    audioUnderflows.fetch_add(1,std::memory_order_relaxed);
}

bool ensureWiFi() {
  if(WiFi.status()==WL_CONNECTED) return true;
  audioState.store(1);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  for(int i=0;i<wifiCount;i++) {
    Serial.printf("WiFi: proba sieci %d/%d\n",i+1,wifiCount);
    WiFi.begin(ssidList[i],passList[i]);
    uint32_t started=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-started<15000) vTaskDelay(pdMS_TO_TICKS(100));
    if(WiFi.status()==WL_CONNECTED) {
      Serial.printf("WiFi polaczone, RSSI %d dBm\n",WiFi.RSSI());
      return true;
    }
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  Serial.println("WiFi: brak polaczenia; ponowienie za 5 s.");
  return false;
}

void radioTask(void *) {
  for(;;) {
    if(!ensureWiFi()) {vTaskDelay(pdMS_TO_TICKS(5000));continue;}
    if(audioMode.load()==PlaybackMode::Stop||!audioUriReady.load()){audioState.store(0);vTaskDelay(pdMS_TO_TICKS(50));continue;}
    uint32_t sessionEpoch=audioStopEpoch.load();
    char sessionUri[1536];
    portENTER_CRITICAL(&audioUriMux);
    memcpy(sessionUri,audioUri,sizeof(sessionUri));
    const AudioCodec codec=requestedCodec;
    portEXIT_CRITICAL(&audioUriMux);
    const bool flac=codec==AudioCodec::FLAC;
    const size_t capacity=min(networkAudioBufferSize,flac?flacBufferCapacity:mp3BufferCapacity);
    audioCodec.store(codec);
    audioBufferCapacity.store(capacity);
    audioState.store(2);
    audioLastMs.store(0);
    audioSamples.store(0);
    audioMutedSamples.store(0);
    audioBitrate.store(0);
    audioRate.store(0);
    Serial.printf("Audio: otwieranie %s; heap=%u\n",flac?"FLAC":"MP3",ESP.getFreeHeap());
    auto *source=new(std::nothrow) CooperativeRadioSource(sessionEpoch,flac);
    if(!source) {audioState.store(4);vTaskDelay(pdMS_TO_TICKS(5000));continue;}
    // ESP8266Audio reads the raw socket, without decoding HTTP chunk framing.
    // Request a close-delimited body for live streams from Music Assistant.
    source->useHTTP10();
    source->RegisterStatusCB(sourceStatus,nullptr);
    if(!source->open(sessionUri)) {
      delete source;audioState.store(4);
      vTaskDelay(pdMS_TO_TICKS(5000));continue;
    }
    auto *buffer=new(std::nothrow) SessionAudioBuffer(source,sessionEpoch,capacity);
    auto *output=new(std::nothrow) MonitoredI2S();
    AudioGenerator *decoder=flac?static_cast<AudioGenerator *>(new(std::nothrow) MonitoredFLAC())
                                :static_cast<AudioGenerator *>(new(std::nothrow) MonitoredMP3());
    bool ok=buffer && output && decoder;
    bool decoderWasBegun=false;
    ok=ok&&audioMode.load()!=PlaybackMode::Stop&&sessionEpoch==audioStopEpoch.load();
    if(ok) {
      buffer->RegisterStatusCB(bufferStatus,nullptr);
      // Piny i wzmocnienie z oryginału. Jawne bufory dla obecnej biblioteki.
      ok=output->SetPinout(DAC_BCK,DAC_LRCK,DAC_DIN) && output->SetBuffers(8,1024);
      output->SetGain(.5f*audioVolume.load()/100.f);
      output->SetChannels(2);
      // Obecna biblioteka wysyła 16-bitowe próbki bez SetBitsPerSample().
      output->SetOutputModeMono(false);
      output->SetRate(44100);
      if(ok) {decoderWasBegun=true;ok=decoder->begin(buffer,output);}
    }
    if(ok) {
      audioConnections.fetch_add(1);
      audioState.store(3);
      Serial.printf("Audio: dekoder uruchomiony na rdzeniu %d; heap=%u\n",xPortGetCoreID(),ESP.getFreeHeap());
      uint32_t lastPublished=0;
      uint32_t lastStackCheck=0;
      uint32_t rateWindowSamples=0,rateWindowBytes=0;
      while(audioMode.load()!=PlaybackMode::Stop && sessionEpoch==audioStopEpoch.load() && WiFi.status()==WL_CONNECTED && decoder->isRunning()) {
        output->SetGain(.5f*audioVolume.load()/100.f);
        if(!decoder->loop()) break;
        uint32_t samples=audioSamples.load(std::memory_order_relaxed);
        if(samples && samples!=lastPublished) {
          if(flac) {
            const uint32_t rate=static_cast<MonitoredFLAC *>(decoder)->rate();
            const uint32_t frames=samples-rateWindowSamples;
            audioRate.store(rate,std::memory_order_relaxed);
            if(rate && frames>=rate*4) {
              const uint32_t bytes=buffer->consumedBytes();
              const uint32_t kbps=uint64_t(bytes-rateWindowBytes)*rate*8/frames/1000;
              audioBitrate.store((kbps+5)/10*10,std::memory_order_relaxed);
              rateWindowSamples=samples;
              rateWindowBytes=bytes;
            }
          }else{
            auto *mp3=static_cast<MonitoredMP3 *>(decoder);
            audioBitrate.store(mp3->bitrate(),std::memory_order_relaxed);
            audioRate.store(mp3->sampleRate(),std::memory_order_relaxed);
          }
          audioFill.store(buffer->getFillLevel(),std::memory_order_relaxed);
          lastPublished=samples;
        }
        if(millis()-lastStackCheck>=1000) {
          audioStackFree.store(uxTaskGetStackHighWaterMark(nullptr),std::memory_order_relaxed);
          lastStackCheck=millis();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    audioState.store(4);
    audioLastMs.store(0);
    audioBitrate.store(0);
    audioFill.store(0);
    if(decoder) {if(decoderWasBegun) decoder->stop();delete decoder;}
    if(output) {output->stop();delete output;}
    if(buffer) {buffer->close();delete buffer;}
    source->close();delete source;
    if(audioMode.load()==PlaybackMode::Stop){
      audioState.store(0);
      Serial.println("Audio: STOP, strumien zamkniety; czekam na Play.");
    }else{
      Serial.printf("Audio: przerwano, ponowienie za 5 s; heap=%u\n",ESP.getFreeHeap());
      for(int i=0;i<100&&audioMode.load()!=PlaybackMode::Stop&&sessionEpoch==audioStopEpoch.load();i++)vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

bool radioIsReceiving() {
  uint32_t last=audioLastMs.load(std::memory_order_relaxed);
  return playbackStreamHealthy(audioMode.load(),audioState.load(std::memory_order_relaxed),last,millis());
}

bool radioReelsMoving(bool animationEnabled=true){
  return !tapeSeekDirection()&&playbackReelsMoving(animationEnabled,audioMode.load(),radioIsReceiving());
}
const char *radioPlaybackState(){
  PlaybackMode mode=audioMode.load();
  if(mode==PlaybackMode::Stop)return audioState.load()==0?"stopped":"stopping";
  if(mode==PlaybackMode::Pause)return "paused";
  return radioIsReceiving()?"playing":"buffering";
}

void startRadioTask() {
  for(size_t capacity : {flacBufferCapacity,mp3BufferCapacity}) {
    auto *largerBuffer=static_cast<uint8_t *>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(largerBuffer){networkAudioBuffer=largerBuffer;networkAudioBufferSize=capacity;break;}
  }
  audioBufferCapacity.store(min(networkAudioBufferSize,mp3BufferCapacity));
  if(xTaskCreatePinnedToCore(radioTask,"radio_audio",12288,nullptr,2,&audioTaskHandle,0)!=pdPASS) {
    audioState.store(4);
    Serial.println("BLAD: nie mozna utworzyc zadania audio.");
  }
}
