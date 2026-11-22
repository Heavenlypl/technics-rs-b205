#pragma once

// Copy to network_private.h and enter your own Wi-Fi settings.
// Add matching entries to both lists if you use more than one network.
static constexpr const char *ssidList[] = {"YOUR_WIFI_SSID"};
static constexpr const char *passList[] = {"YOUR_WIFI_PASSWORD"};
static constexpr int wifiCount = sizeof(ssidList) / sizeof(ssidList[0]);
static_assert(wifiCount == sizeof(passList) / sizeof(passList[0]),
              "Provide one password per Wi-Fi network.");
