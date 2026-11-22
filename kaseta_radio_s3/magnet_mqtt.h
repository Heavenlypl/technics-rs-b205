#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <cJSON.h>
#include <atomic>
#include "magnet_mqtt_protocol.h"
#include "stations.h"
#include "radio_audio.h"
#if __has_include("mqtt_private.h")
#include "mqtt_private.h"
#else
#include "mqtt_config.example.h"
#endif

// The UI owns all rendering. Network callbacks pass only validated preset indices.
static QueueHandle_t magnetLabelQueue=nullptr,magnetEdgeQueue=nullptr;
struct MagnetEdge {uint32_t epoch,at;uint8_t key,down;};
static std::atomic<uint32_t> magnetEpoch{0},magnetDrops{0};
static std::atomic<uint32_t> magnetPrevPresses{0},magnetNextPresses{0};
static std::atomic<uint32_t> magnetLocalRewindMs[2]{};
static std::atomic<bool> magnetOnline{false};
static std::atomic<int> magnetAppliedPreset{1};
static bool magnetRediscover=false;
static WiFiClient magnetTCP;
static WiFiClientSecure magnetTLS;
static PubSubClient magnetMQTT;
static char magnetMac[13],magnetClientID[40];

static int magnetPresetFromJSON(const char *data,size_t length){
 if(!length||length>384||memchr(data,0,length))return -1;
 const char *end=nullptr;
 cJSON *obj=cJSON_ParseWithLengthOpts(data,length,&end,false);
 if(!obj)return -1;
 // Reject trailing non-whitespace and non-object payloads.
 while(end<data+length&&(*end==' '||*end=='\n'||*end=='\r'||*end=='\t'))++end;
 int result=-1;
 auto *speak=cJSON_GetObjectItemCaseSensitive(obj,"speak"),*media=cJSON_GetObjectItemCaseSensitive(obj,"media");
 if(end==data+length&&cJSON_IsObject(obj)&&cJSON_IsString(speak)&&cJSON_IsString(media)){
  for(size_t i=0;i<STATION_COUNT;i++)if(!strcmp(speak->valuestring,STATION_PRESETS[i].speak)&&!strcmp(media->valuestring,STATION_PRESETS[i].media)){result=int(i);break;}
 }
 cJSON_Delete(obj);return result;
}
static void magnetTapeKey(const char *key,bool physical){
 const bool forward=!strcmp(key,"next");
 if(forward||!strcmp(key,"prev")){
  auto &localTime=magnetLocalRewindMs[forward?1:0];
  uint32_t now=millis();
  if(physical)localTime.store(now);
  else{
   const uint32_t last=localTime.exchange(0);
   if(last&&uint32_t(now-last)<1500)return; // The broker echoed our own key press.
  }
  beginTapeSeek(forward);
 }else if(!strcmp(key,"pause")||!strcmp(key,"stop"))cancelTapeSeek();
}
static void magnetMessage(char *topic,uint8_t *payload,unsigned length){
 static constexpr char keyPrefix[]="technics_rs_b205/key/";
 if(length==2&&!memcmp(payload,"ON",2)&&!strncmp(topic,keyPrefix,sizeof(keyPrefix)-1)){
  magnetTapeKey(topic+sizeof(keyPrefix)-1,false);
 }else if(!strcmp(topic,"homeassistant/status")){
  if(length==6&&!memcmp(payload,"online",6))magnetRediscover=true;
 }else if(!strcmp(topic,MAGNET_PLAYBACK_SET)){
  PlaybackMode mode;
  if(length==12&&!memcmp(payload,"toggle_pause",12))radioTogglePause();
  else if(length==6&&!memcmp(payload,"resume",6))radioResumePaused();
  else if(parsePlaybackCommand(reinterpret_cast<char *>(payload),length,mode))radioRequestMode(mode);
 }else if(!strcmp(topic,MAGNET_STATION_SET)){
  int preset=magnetPresetFromJSON(reinterpret_cast<char *>(payload),length);
  if(preset>=0)xQueueOverwrite(magnetLabelQueue,&preset);
  else Serial.println("MQTT: odrzucono podpis spoza katalogu speak/media.");
 }
}
static void magnetScanTask(void *){
 MagnetContact keys[MAGNET_KEY_COUNT];bool armed[MAGNET_KEY_COUNT]={};uint32_t epoch=magnetEpoch.load();
 for(size_t i=0;i<MAGNET_KEY_COUNT;i++)keys[i].resync(digitalRead(MAGNET_KEYS[i].gpio)==LOW,millis());
 for(;;){
  uint32_t now=millis(),nextEpoch=magnetEpoch.load();bool online=magnetOnline.load();
  bool reset=nextEpoch!=epoch;epoch=nextEpoch;
  for(size_t i=0;i<MAGNET_KEY_COUNT;i++){
   bool down=digitalRead(MAGNET_KEYS[i].gpio)==LOW;
   if(reset||!online)armed[i]=!down;
   int edge=keys[i].update(down,now);
   if(edge>=0){
    Serial.printf("KEY %s=%s mqtt=%d\n",MAGNET_KEYS[i].name,edge?"ON":"OFF",online);
    if(edge){
     if(!strcmp(MAGNET_KEYS[i].id,"prev"))magnetPrevPresses.fetch_add(1,std::memory_order_relaxed);
     if(!strcmp(MAGNET_KEYS[i].id,"next"))magnetNextPresses.fetch_add(1,std::memory_order_relaxed);
     magnetTapeKey(MAGNET_KEYS[i].id,true);
    }
    if(!edge)armed[i]=true;
    if(online&&armed[i]){MagnetEdge e{epoch,now,uint8_t(i),uint8_t(edge)};
     if(xQueueSend(magnetEdgeQueue,&e,0)!=pdTRUE)magnetDrops.fetch_add(1);
    }
   }
  }
  vTaskDelay(pdMS_TO_TICKS(5));
 }
}
static bool magnetAnnounce(){
 magnetOnline.store(false);
 char topic[128],json[1024];
 for(size_t i=0;i<MAGNET_KEY_COUNT;i++){
  if(!magnetDiscovery(i,magnetMac,topic,sizeof(topic),json,sizeof(json))||!magnetMQTT.publish(topic,json,true))return false;
  snprintf(topic,sizeof(topic),"%s/key/%s",MAGNET_BASE,MAGNET_KEYS[i].id);
  // No retained ON states: neither a reboot nor HA restart replays a press.
  if(!magnetMQTT.publish(topic,"OFF",false))return false;
 }
 // Publish OFF states before availability. NR must trigger only from off -> on.
 if(!magnetMQTT.subscribe(MAGNET_PLAYBACK_SET)||!magnetMQTT.subscribe(MAGNET_STATION_SET)||
    !magnetMQTT.subscribe("technics_rs_b205/key/+")||!magnetMQTT.subscribe("homeassistant/status"))return false;
 xQueueReset(magnetEdgeQueue);magnetEpoch.fetch_add(1);
 if(!magnetMQTT.publish(MAGNET_AVAIL,"online",true))return false;
 magnetOnline.store(true);return true;
}
static void magnetNetworkTask(void *){
 magnetMQTT.setClient(MAGNET_MQTT_TLS?static_cast<Client &>(magnetTLS):static_cast<Client &>(magnetTCP));
 if(MAGNET_MQTT_TLS)magnetTLS.setCACert(MAGNET_MQTT_CA);
 magnetTCP.setConnectionTimeout(700);magnetTLS.setConnectionTimeout(700);
 magnetMQTT.setServer(MAGNET_MQTT_HOST,MAGNET_MQTT_PORT);
 magnetMQTT.setCallback(magnetMessage);magnetMQTT.setKeepAlive(15);magnetMQTT.setSocketTimeout(1);
 if(!magnetMQTT.setBufferSize(1536)){Serial.println("MQTT: brak bufora; radio nadal dziala.");vTaskDelete(nullptr);return;}
 uint32_t attempt=millis()-10000,report=millis(),seenDrops=magnetDrops.load();int published=-1;const char *publishedPlayback=nullptr;
 for(;;){
  uint32_t now=millis();
  if(magnetDrops.load()!=seenDrops){seenDrops=magnetDrops.load();magnetOnline.store(false);magnetTCP.stop();magnetTLS.stop();}
  if(WiFi.status()!=WL_CONNECTED){magnetOnline.store(false);magnetTCP.stop();magnetTLS.stop();}
  else if(!magnetMQTT.connected()){
   magnetOnline.store(false);
   if(uint32_t(now-attempt)>=10000){
    attempt=now;magnetRediscover=false;
    bool ok=magnetMQTT.connect(magnetClientID,MAGNET_MQTT_USER,MAGNET_MQTT_PASSWORD,MAGNET_AVAIL,1,true,"offline",true);
    if(ok&&magnetAnnounce()){published=-1;publishedPlayback=nullptr;Serial.println("MQTT: Magnet-Technics online, 6 encji.");}
    else {Serial.printf("MQTT: brak polaczenia/discovery, stan=%d; ponowienie za 10s.\n",magnetMQTT.state());magnetTCP.stop();magnetTLS.stop();}
   }
  }else{
   if(!magnetMQTT.loop()){magnetOnline.store(false);}
   else {
    if(magnetRediscover){magnetRediscover=false;if(!magnetAnnounce()){magnetOnline.store(false);magnetTCP.stop();magnetTLS.stop();}published=-1;publishedPlayback=nullptr;}
    MagnetEdge edge;
    while(magnetOnline.load()&&xQueueReceive(magnetEdgeQueue,&edge,0)==pdTRUE){
     // Discard stale presses, including presses while the broker was unavailable.
     if(edge.epoch!=magnetEpoch.load())continue;
     if(uint32_t(millis()-edge.at)>1000){magnetDrops.fetch_add(1);magnetOnline.store(false);magnetTCP.stop();magnetTLS.stop();break;}
     char topic[80];snprintf(topic,sizeof(topic),"%s/key/%s",MAGNET_BASE,MAGNET_KEYS[edge.key].id);
     if(!magnetMQTT.publish(topic,edge.down?"ON":"OFF",false)){magnetOnline.store(false);magnetTCP.stop();magnetTLS.stop();break;}
    }
    const char *playback=radioPlaybackState();
    if(magnetOnline.load()&&(!publishedPlayback||strcmp(publishedPlayback,playback))&&magnetMQTT.publish(MAGNET_PLAYBACK_STATE,playback,true))publishedPlayback=playback;
    int applied=magnetAppliedPreset.load();
    if(magnetOnline.load()&&applied!=published){
     cJSON *obj=cJSON_CreateObject();
     if(obj){cJSON_AddNumberToObject(obj,"index",applied);cJSON_AddStringToObject(obj,"speak",STATION_PRESETS[applied].speak);cJSON_AddStringToObject(obj,"media",STATION_PRESETS[applied].media);cJSON_AddBoolToObject(obj,"display_only",true);
      char *data=cJSON_PrintUnformatted(obj);if(data){if(magnetMQTT.publish(MAGNET_STATION_STATE,data,true))published=applied;cJSON_free(data);}cJSON_Delete(obj);}
    }
   }
  }
  if(uint32_t(now-report)>=10000){report=now;Serial.printf("MQTT online=%d drops=%lu stack=%u\n",magnetOnline.load(),(unsigned long)magnetDrops.load(),unsigned(uxTaskGetStackHighWaterMark(nullptr)));}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
static void startMagnetHA(){
 for(const auto &key:MAGNET_KEYS)pinMode(key.gpio,INPUT_PULLUP);
 if(xTaskCreatePinnedToCore(magnetScanTask,"magnet_keys",2048,nullptr,2,nullptr,1)!=pdPASS){Serial.println("MQTT: brak task klawiszy.");return;}
 if(!MAGNET_MQTT_ENABLED){Serial.println("MQTT: konfiguracja w mqtt_private.h oczekuje na dane logowania; radio bez zmian.");return;}
 if(!*MAGNET_MQTT_HOST||(MAGNET_MQTT_TLS&&!*MAGNET_MQTT_CA)){Serial.println("MQTT: niepelna konfiguracja host/CA.");return;}
 uint64_t mac=ESP.getEfuseMac();snprintf(magnetMac,sizeof(magnetMac),"%02x%02x%02x%02x%02x%02x",unsigned(mac&255),unsigned((mac>>8)&255),unsigned((mac>>16)&255),unsigned((mac>>24)&255),unsigned((mac>>32)&255),unsigned((mac>>40)&255));
 snprintf(magnetClientID,sizeof(magnetClientID),"technics_rs_b205_%s",magnetMac);
 magnetLabelQueue=xQueueCreate(1,sizeof(int));magnetEdgeQueue=xQueueCreate(32,sizeof(MagnetEdge));
 if(!magnetLabelQueue||!magnetEdgeQueue){Serial.println("MQTT: brak RAM kolejek.");return;}
 if(xTaskCreatePinnedToCore(magnetNetworkTask,"magnet_mqtt",6144,nullptr,1,nullptr,0)!=pdPASS)Serial.println("MQTT: brak task sieci.");
}
static bool magnetTakeLabel(int &preset){return magnetLabelQueue&&xQueueReceive(magnetLabelQueue,&preset,0)==pdTRUE;}
