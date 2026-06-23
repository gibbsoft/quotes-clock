#include "platform_state.hpp"

#include <cstdio>

#include <esp_mac.h>
#include <esp_wifi.h>

#include "app_config.hpp"

namespace quotes_clock {
namespace {
constexpr const char *kDeviceHostname = "quotes-clock";
constexpr const char *kDeviceLanFqdn = "quotes-clock.local";

std::string format_mac(esp_mac_type_t type) {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, type) != ESP_OK)
    return {};

  char buf[18] = {};
  std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}
}  // namespace

const char *device_hostname() {
  return kDeviceHostname;
}

const char *device_lan_fqdn() {
  return kDeviceLanFqdn;
}

std::string fallback_ap_ssid() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK)
    return "quotes-clock-setup";

  char buf[24] = {};
  std::snprintf(buf, sizeof(buf), "quotes-clock-%02x%02x", mac[4], mac[5]);
  return buf;
}

PlatformSnapshot platform_snapshot() {
  PlatformSnapshot snapshot;
  snapshot.fallback_ap_ssid = fallback_ap_ssid();
  snapshot.hostname = device_hostname();
  snapshot.lan_fqdn = device_lan_fqdn();
  snapshot.station_mac = format_mac(ESP_MAC_WIFI_STA);
  snapshot.ap_mac = format_mac(ESP_MAC_WIFI_SOFTAP);

  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    snapshot.wifi_connected = true;
    snapshot.wifi_rssi = ap.rssi;
    if (ap.ssid[0])
      snapshot.station_ssid = reinterpret_cast<const char *>(ap.ssid);
  } else {
    const auto network = AppConfig::instance().network();
    if (!network.ssid.empty())
      snapshot.station_ssid = network.ssid;
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) == ESP_OK)
    snapshot.fallback_ap_active = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) && !snapshot.wifi_connected;

  return snapshot;
}

}  // namespace quotes_clock
