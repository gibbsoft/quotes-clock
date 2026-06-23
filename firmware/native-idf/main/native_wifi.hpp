#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "app_config.hpp"

namespace quotes_clock {

struct WifiStatus {
  bool sta_configured = false;
  bool connected = false;
  bool got_ip = false;
  bool fallback_ap_active = false;
  int rssi = -127;
  uint8_t reconnects = 0;
  int last_disconnect_reason = 0;
  std::string ip_address;
  std::string hostname;
};

class NativeWifi {
 public:
  NativeWifi(const NativeWifi &) = delete;

  static NativeWifi &instance();

  esp_err_t start();
  esp_err_t apply_config(const NetworkConfig &config);
  esp_err_t reconnect_station();
  WifiStatus status() const;
  esp_netif_t *sta_netif() const {
    return _sta_netif;
  }

 private:
  NativeWifi() = default;

  static void wifi_event_thunk(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void ip_event_thunk(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  void handle_wifi_event(int32_t event_id, void *event_data);
  void handle_ip_event(int32_t event_id, void *event_data);
  esp_err_t configure_ap();
  esp_err_t configure_sta();
  esp_err_t apply_static_ip();
  esp_err_t set_fallback_ap_active(bool active);

  esp_netif_t *_sta_netif = nullptr;
  esp_netif_t *_ap_netif = nullptr;
  NetworkConfig _config;
  std::atomic<bool> _started{false};
  std::atomic<bool> _connected{false};
  std::atomic<bool> _got_ip{false};
  std::atomic<bool> _fallback_ap_active{false};
  std::atomic<int> _rssi{-127};
  std::atomic<uint8_t> _reconnects{0};
  std::atomic<int> _last_disconnect_reason{0};
};

}  // namespace quotes_clock
