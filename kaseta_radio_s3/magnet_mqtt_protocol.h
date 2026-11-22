#pragma once
#include <stdio.h>
#include <string.h>
#include "magnet_buttons.h"
constexpr const char *MAGNET_BASE="technics_rs_b205";
constexpr const char *MAGNET_AVAIL="technics_rs_b205/availability";
constexpr const char *MAGNET_PLAYBACK_SET="technics_rs_b205/playback/set";
constexpr const char *MAGNET_PLAYBACK_STATE="technics_rs_b205/playback/state";
constexpr const char *MAGNET_STATION_SET="technics_rs_b205/station/set";
constexpr const char *MAGNET_STATION_STATE="technics_rs_b205/station/state";
inline bool magnetDiscovery(size_t key,const char *mac,char *topic,size_t topicSize,char *json,size_t jsonSize){
 if(key>=MAGNET_KEY_COUNT)return false;
 const auto &k=MAGNET_KEYS[key];
 int t=snprintf(topic,topicSize,"homeassistant/binary_sensor/technics_rs_b205_%s_%s/config",mac,k.id);
 int n=snprintf(json,jsonSize,
  "{\"name\":\"%s\",\"unique_id\":\"technics_rs_b205_%s_%s\","
  "\"default_entity_id\":\"binary_sensor.technics_rs_b205_%s\","
  "\"state_topic\":\"technics_rs_b205/key/%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
  "\"availability_topic\":\"technics_rs_b205/availability\",\"payload_available\":\"online\","
  "\"payload_not_available\":\"offline\",\"icon\":\"%s\",\"qos\":0,"
  "\"device\":{\"identifiers\":[\"technics_rs_b205_%s\"],\"name\":\"Magnet-Technics\","
  "\"manufacturer\":\"Technics / DIY\",\"model\":\"RS-B205 ESP32-S3\",\"sw_version\":\"7-playback\"}}",
  k.name,mac,k.id,k.id,k.id,k.icon,mac);
 return t>0&&size_t(t)<topicSize&&n>0&&size_t(n)<jsonSize;
}
