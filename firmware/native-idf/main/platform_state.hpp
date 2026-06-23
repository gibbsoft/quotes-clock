#pragma once

#include <string>

namespace quotes_clock {

struct PlatformSnapshot {
  bool wifi_connected = false;
  int wifi_rssi = -127;
  bool fallback_ap_active = true;
  std::string station_ssid = "saved network";
  std::string fallback_ap_ssid = "quotes-clock-setup";
  std::string hostname = "quotes-clock";
  std::string lan_fqdn = "quotes-clock.local";
  std::string station_mac;
  std::string ap_mac;
};

PlatformSnapshot platform_snapshot();
std::string fallback_ap_ssid();
const char *device_hostname();
const char *device_lan_fqdn();

}  // namespace quotes_clock
