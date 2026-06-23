#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <nvs_flash.h>

#include "app_config.hpp"
#include "native_display.hpp"
#include "native_https_server.hpp"
#include "native_sntp.hpp"
#include "native_wifi.hpp"

namespace {
constexpr const char *kTag = "quotes_clock_native";
}

extern "C" void app_main() {
  esp_err_t nvs_result = nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_result);

  esp_err_t ota_valid_result = esp_ota_mark_app_valid_cancel_rollback();
  if (ota_valid_result == ESP_OK) {
    ESP_LOGI(kTag, "marked OTA app valid");
  } else if (ota_valid_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "unable to mark OTA app valid: %s", esp_err_to_name(ota_valid_result));
  }

  ESP_ERROR_CHECK(quotes_clock::AppConfig::instance().load());
  quotes_clock::NativeDisplay::instance().set_options(quotes_clock::AppConfig::instance().display());
  ESP_ERROR_CHECK(quotes_clock::NativeWifi::instance().start());
  ESP_ERROR_CHECK(quotes_clock::NativeSntp::instance().start());
  ESP_ERROR_CHECK(quotes_clock::NativeHttpsServer::instance().start());
  ESP_ERROR_CHECK(quotes_clock::NativeDisplay::instance().start());
  ESP_LOGI(kTag, "native IDF spike started");
}
