#include "native_wifi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <apps/esp_sntp.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <lwip/ip4_addr.h>
#include <lwip/inet.h>

#include "native_display.hpp"
#include "platform_state.hpp"

namespace quotes_clock {
namespace {
constexpr const char *kTag = "native_wifi";
constexpr uint8_t kMaxReconnectsBeforeSetupAp = 4;

bool parse_ipv4(const std::string &value, esp_ip4_addr_t &out) {
  if (value.empty())
    return false;
  return ip4addr_aton(value.c_str(), reinterpret_cast<ip4_addr_t *>(&out)) != 0;
}

std::string format_ipv4(const esp_ip4_addr_t &addr) {
  char buf[16] = {};
  std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&addr));
  return buf;
}
}  // namespace

NativeWifi &NativeWifi::instance() {
  static NativeWifi wifi;
  return wifi;
}

esp_err_t NativeWifi::start() {
  if (_started.exchange(true))
    return ESP_OK;

  _config = AppConfig::instance().network();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  _sta_netif = esp_netif_create_default_wifi_sta();
  _ap_netif = esp_netif_create_default_wifi_ap();
  if (_sta_netif)
    ESP_ERROR_CHECK(esp_netif_set_hostname(_sta_netif, device_hostname()));

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_thunk, this, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_thunk, this, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(configure_ap());
  ESP_ERROR_CHECK(configure_sta());
  ESP_ERROR_CHECK(esp_wifi_start());

  const bool has_sta = !_config.ssid.empty();
  ESP_ERROR_CHECK(set_fallback_ap_active(!has_sta));
  if (has_sta) {
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
      return err;
  }

  ESP_LOGI(kTag, "started native Wi-Fi: sta=%s fallback_ap=%s", has_sta ? _config.ssid.c_str() : "(none)",
           fallback_ap_ssid().c_str());
  return ESP_OK;
}

esp_err_t NativeWifi::apply_config(const NetworkConfig &config) {
  _config = config;
  ESP_ERROR_CHECK(AppConfig::instance().set_network(config));
  if (!_started.load())
    return ESP_OK;

  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
    return err;
  ESP_ERROR_CHECK(configure_sta());
  _connected.store(false);
  _got_ip.store(false);
  _reconnects.store(0);
  _last_disconnect_reason.store(0);
  ESP_ERROR_CHECK(set_fallback_ap_active(true));
  if (!_config.ssid.empty()) {
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
      return err;
  }
  return ESP_OK;
}

esp_err_t NativeWifi::reconnect_station() {
  if (!_started.load() || _config.ssid.empty())
    return ESP_OK;

  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
    return err;
  err = configure_sta();
  if (err != ESP_OK)
    return err;
  _connected.store(false);
  _got_ip.store(false);
  _reconnects.store(0);
  _last_disconnect_reason.store(0);
  err = esp_wifi_connect();
  return (err == ESP_ERR_WIFI_CONN) ? ESP_OK : err;
}

WifiStatus NativeWifi::status() const {
  WifiStatus status;
  status.sta_configured = !_config.ssid.empty();
  status.connected = _connected.load();
  status.got_ip = _got_ip.load();
  status.fallback_ap_active = _fallback_ap_active.load();
  status.rssi = _rssi.load();
  status.reconnects = _reconnects.load();
  status.last_disconnect_reason = _last_disconnect_reason.load();
  status.hostname = device_hostname();
  if (_sta_netif && status.got_ip) {
    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(_sta_netif, &ip_info) == ESP_OK)
      status.ip_address = format_ipv4(ip_info.ip);
  }
  return status;
}

void NativeWifi::wifi_event_thunk(void *arg, esp_event_base_t, int32_t event_id, void *event_data) {
  static_cast<NativeWifi *>(arg)->handle_wifi_event(event_id, event_data);
}

void NativeWifi::ip_event_thunk(void *arg, esp_event_base_t, int32_t event_id, void *event_data) {
  static_cast<NativeWifi *>(arg)->handle_ip_event(event_id, event_data);
}

void NativeWifi::handle_wifi_event(int32_t event_id, void *event_data) {
  switch (event_id) {
    case WIFI_EVENT_STA_START:
      if (!_config.ssid.empty())
        esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_CONNECTED: {
      auto *event = static_cast<wifi_event_sta_connected_t *>(event_data);
      _connected.store(true);
      _rssi.store(-127);
      _reconnects.store(0);
      _last_disconnect_reason.store(0);
      ESP_LOGI(kTag, "connected to %.*s", event ? static_cast<int>(event->ssid_len) : 0,
               event ? reinterpret_cast<char *>(event->ssid) : "");
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      auto *event = static_cast<wifi_event_sta_disconnected_t *>(event_data);
      _connected.store(false);
      _got_ip.store(false);
      _last_disconnect_reason.store(event ? event->reason : 0);
      const uint8_t reconnects = static_cast<uint8_t>(_reconnects.load() + 1);
      _reconnects.store(reconnects);
      if (reconnects >= kMaxReconnectsBeforeSetupAp)
        set_fallback_ap_active(true);
      if (!_config.ssid.empty())
        esp_wifi_connect();
      ESP_LOGW(kTag, "station disconnected, reason=%d reconnect=%u", event ? event->reason : 0, reconnects);
      break;
    }
    default:
      break;
  }
}

void NativeWifi::handle_ip_event(int32_t event_id, void *event_data) {
  if (event_id != IP_EVENT_STA_GOT_IP)
    return;

  auto *event = static_cast<ip_event_got_ip_t *>(event_data);
  _got_ip.store(true);
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    _rssi.store(ap.rssi);
  set_fallback_ap_active(false);
  NativeDisplay::instance().request_refresh(false);
  ESP_LOGI(kTag, "station got IP: " IPSTR " hostname=%s", IP2STR(&event->ip_info.ip), device_hostname());
}

esp_err_t NativeWifi::configure_ap() {
  wifi_config_t ap = {};
  const auto ssid = fallback_ap_ssid();
  std::strncpy(reinterpret_cast<char *>(ap.ap.ssid), ssid.c_str(), sizeof(ap.ap.ssid));
  ap.ap.ssid_len = std::min<size_t>(ssid.size(), sizeof(ap.ap.ssid));
  ap.ap.channel = 6;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ap.ap.pmf_cfg.required = false;
  return esp_wifi_set_config(WIFI_IF_AP, &ap);
}

esp_err_t NativeWifi::configure_sta() {
#if CONFIG_LWIP_DHCP_GET_NTP_SRV
  esp_sntp_servermode_dhcp(AppConfig::instance().time().ntp_mode == NtpMode::Dhcp);
#endif

  if (_config.static_ip)
    ESP_ERROR_CHECK(apply_static_ip());
  else if (_sta_netif)
    (void)esp_netif_dhcpc_start(_sta_netif);

  wifi_config_t sta = {};
  std::strncpy(reinterpret_cast<char *>(sta.sta.ssid), _config.ssid.c_str(), sizeof(sta.sta.ssid));
  std::strncpy(reinterpret_cast<char *>(sta.sta.password), _config.password.c_str(), sizeof(sta.sta.password));
  sta.sta.threshold.authmode = _config.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

esp_err_t NativeWifi::apply_static_ip() {
  if (!_sta_netif)
    return ESP_ERR_INVALID_STATE;

  esp_netif_ip_info_t ip_info = {};
  if (!parse_ipv4(_config.ip, ip_info.ip) || !parse_ipv4(_config.gateway, ip_info.gw) ||
      !parse_ipv4(_config.netmask, ip_info.netmask)) {
    ESP_LOGW(kTag, "static IP config incomplete; leaving DHCP enabled");
    return ESP_OK;
  }

  ESP_ERROR_CHECK(esp_netif_dhcpc_stop(_sta_netif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(_sta_netif, &ip_info));

  for (size_t i = 0; i < _config.dns.size(); i++) {
    esp_ip4_addr_t dns_ip = {};
    if (!parse_ipv4(_config.dns[i], dns_ip))
      continue;
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = dns_ip;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(_sta_netif, i == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP, &dns));
  }
  return ESP_OK;
}

esp_err_t NativeWifi::set_fallback_ap_active(bool active) {
  const bool has_sta = !_config.ssid.empty();
  wifi_mode_t mode = active ? (has_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP) : WIFI_MODE_STA;
  esp_err_t err = esp_wifi_set_mode(mode);
  if (err == ESP_OK)
    _fallback_ap_active.store(active);
  return err;
}

}  // namespace quotes_clock
