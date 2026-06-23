#include "native_sntp.hpp"

#include <ctime>

#include <apps/esp_sntp.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_netif_types.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>

#include "native_display.hpp"
#include "native_wifi.hpp"

namespace quotes_clock {
namespace {
constexpr const char *kTag = "native_sntp";
constexpr const char *kDefaultNtpServer = "pool.ntp.org";
constexpr size_t kServerCount = 3;

uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
}  // namespace

NativeSntp &NativeSntp::instance() {
  static NativeSntp sntp;
  return sntp;
}

esp_err_t NativeSntp::start() {
  if (_started.exchange(true))
    return ESP_OK;
  _config = AppConfig::instance().time();
  sntp_set_time_sync_notification_cb(sync_cb);
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_thunk, this, nullptr));
  update();
  return ESP_OK;
}

esp_err_t NativeSntp::apply_config(const TimeConfig &config) {
  _config = config;
  ESP_ERROR_CHECK(AppConfig::instance().set_time(config));
  if (_started.load()) {
    update();
    if (config.ntp_mode == NtpMode::Dhcp) {
      const esp_err_t err = NativeWifi::instance().reconnect_station();
      if (err != ESP_OK)
        return err;
    }
  }
  return ESP_OK;
}

SntpStatus NativeSntp::status() const {
  const uint32_t synced_at = _synced_at_ms.load();
  SntpStatus status;
  status.enabled = esp_sntp_enabled();
  status.mode = static_cast<int>(_config.ntp_mode);
  status.sync_status = static_cast<int>(sntp_get_sync_status());
  status.last_sync_age_ms = synced_at == 0 ? 0 : millis() - synced_at;
  for (uint8_t i = 0; i < kServerCount; i++) {
    const char *name = esp_sntp_getservername(i);
    if (name != nullptr && name[0] != '\0') {
      status.servers[i] = name;
      continue;
    }
    const ip_addr_t *addr = esp_sntp_getserver(i);
    if (addr == nullptr || ip_addr_isany(addr))
      continue;
    char buffer[IPADDR_STRLEN_MAX] = {};
    if (ipaddr_ntoa_r(addr, buffer, sizeof(buffer)) != nullptr)
      status.servers[i] = buffer;
  }
  return status;
}

void NativeSntp::sync_cb(struct timeval *) {
  NativeSntp::instance()._synced_at_ms.store(millis());
  time_t now = time(nullptr);
  tm local = {};
  localtime_r(&now, &local);
  char buf[32] = {};
  strftime(buf, sizeof(buf), "%F %T", &local);
  ESP_LOGI(kTag, "time synchronised: %s", buf);
  NativeDisplay::instance().request_refresh(false);
}

void NativeSntp::ip_event_thunk(void *arg, esp_event_base_t, int32_t event_id, void *) {
  if (event_id == IP_EVENT_STA_GOT_IP)
    static_cast<NativeSntp *>(arg)->update();
}

void NativeSntp::update() {
  esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  sntp_set_sync_interval(60 * 60 * 1000);

#if CONFIG_LWIP_DHCP_GET_NTP_SRV
  esp_sntp_servermode_dhcp(_config.ntp_mode == NtpMode::Dhcp);
#endif

  if (_config.ntp_mode == NtpMode::DefaultPool) {
    for (uint8_t i = 0; i < kServerCount; i++) {
      esp_sntp_setserver(i, nullptr);
      esp_sntp_setservername(i, nullptr);
    }
    esp_sntp_setservername(0, kDefaultNtpServer);
  } else if (_config.ntp_mode == NtpMode::Manual) {
    for (uint8_t i = 0; i < kServerCount; i++) {
      esp_sntp_setserver(i, nullptr);
      esp_sntp_setservername(i, nullptr);
    }
    for (uint8_t i = 0; i < kServerCount; i++) {
      if (!_config.ntp_servers[i].empty())
        esp_sntp_setservername(i, _config.ntp_servers[i].c_str());
    }
  }

  esp_sntp_init();
  sntp_restart();
  ESP_LOGI(kTag, "SNTP updated: mode=%d server0=%s", static_cast<int>(_config.ntp_mode),
           _config.ntp_mode == NtpMode::DefaultPool ? kDefaultNtpServer : _config.ntp_servers[0].c_str());
}

}  // namespace quotes_clock
