#pragma once
// Copy to mqtt_private.h (gitignored). Credentials never printed to UART.
// HA core-mosquitto is an INTERNAL hostname; use the HA LAN IP from the ESP.
constexpr bool MAGNET_MQTT_ENABLED=false; // enable after supplying existing MQTT login
constexpr const char *MAGNET_MQTT_HOST="mqtt.example.invalid";
constexpr uint16_t MAGNET_MQTT_PORT=1883;
constexpr const char *MAGNET_MQTT_USER="";
constexpr const char *MAGNET_MQTT_PASSWORD="";
// For a TLS broker select its TLS port, true, and its CA certificate.
constexpr bool MAGNET_MQTT_TLS=false;
constexpr const char *MAGNET_MQTT_CA="";
