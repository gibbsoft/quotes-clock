#include "app_config.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <esp_random.h>
#include <nvs.h>
#include <sha/sha_core.h>

namespace quotes_clock {
namespace {
constexpr const char *kNamespace = "qc";
constexpr int kQuoteCadenceMin = 1;
constexpr int kQuoteCadenceMax = 1440;

template <size_t N>
bool load_blob(nvs_handle_t handle, const char *key, uint8_t (&out)[N]) {
  size_t len = N;
  return nvs_get_blob(handle, key, out, &len) == ESP_OK && len == N;
}
}  // namespace

AppConfig &AppConfig::instance() {
  static AppConfig config;
  return config;
}

esp_err_t AppConfig::load() {
  std::lock_guard<std::mutex> guard(_mutex);
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return ESP_OK;
  if (err != ESP_OK)
    return err;

  _admin_configured = load_blob(handle, "pwd_salt", _password_salt) && load_blob(handle, "pwd_hash", _password_hash);
  nvs_close(handle);

  _network.ssid = load_string("wifi_ssid");
  _network.password = load_string("wifi_pass");
  _network.static_ip = load_u8("sta_static", 0) != 0;
  _network.ip = load_string("sta_ip");
  _network.netmask = load_string("sta_mask");
  _network.gateway = load_string("sta_gw");
  _network.dns[0] = load_string("dns1");
  _network.dns[1] = load_string("dns2");

  _display.display_enabled = load_bool("disp_en", _display.display_enabled);
  _display.layout = std::clamp<int>(load_i32("layout", _display.layout), 0, 3);
  _display.refresh_cadence_minutes =
      std::clamp<int>(load_i32("cadence", _display.refresh_cadence_minutes), kQuoteCadenceMin, kQuoteCadenceMax);
  _display.timezone = std::clamp<int>(load_i32("tz", _display.timezone), 0, 9);
  _display.dst_mode = std::clamp<int>(load_i32("dst", _display.dst_mode), 0, 2);
  _display.clock_format = std::clamp<int>(load_i32("clock_fmt", _display.clock_format), 0, 1);
  _display.content_margin = std::clamp<int>(load_i32("margin", _display.content_margin), 16, 72);
  _display.clock_visible = load_bool("clock_vis", _display.clock_visible);
  _display.quote_visible = load_bool("quote_vis", _display.quote_visible);
  _display.quote_time_specific_enabled = load_bool("quote_time", _display.quote_time_specific_enabled);
  _display.quote_classics_enabled = load_bool("quote_class", _display.quote_classics_enabled);
  if (!_display.quote_time_specific_enabled && !_display.quote_classics_enabled)
    _display.quote_time_specific_enabled = true;
  _display.highlight_time_enabled = load_bool("hl_time", _display.highlight_time_enabled);
  _display.highlight_time_color = std::clamp<int>(load_i32("hl_col", _display.highlight_time_color), 0, 9);
  _display.highlight_time_text_color = std::clamp<int>(load_i32("hl_txt", _display.highlight_time_text_color), 0, 9);
  _display.main_pane_bg_color = std::clamp<int>(load_i32("main_bg", _display.main_pane_bg_color), 0, 9);
  _display.main_pane_text_color = std::clamp<int>(load_i32("main_txt", _display.main_pane_text_color), 0, 9);
  _display.sidebar_visible = load_bool("side_vis", _display.sidebar_visible);
  _display.sidebar_color = std::clamp<int>(load_i32("side_col", _display.sidebar_color), 0, 9);
  _display.bottom_bar_visible = load_bool("bot_vis", _display.bottom_bar_visible);
  _display.bottom_bar_bg_color = std::clamp<int>(load_i32("bot_bg", _display.bottom_bar_bg_color), 0, 9);
  _display.bottom_bar_text_color = std::clamp<int>(load_i32("bot_txt", _display.bottom_bar_text_color), 0, 9);
  _display.top_bar_visible = load_bool("top_vis", _display.top_bar_visible);
  _display.top_bar_bg_color = std::clamp<int>(load_i32("top_bg", _display.top_bar_bg_color), 0, 9);
  _display.top_bar_text_color = std::clamp<int>(load_i32("top_txt", _display.top_bar_text_color), 0, 9);
  _display.top_bar_date_format = std::clamp<int>(load_i32("top_date", _display.top_bar_date_format), 0, 5);
  _display.watch_style = load_bool("watch_style", _display.watch_style);

  _time.ntp_mode = static_cast<NtpMode>(std::clamp<int>(load_i32("ntp_mode", 0), 0, 2));
  _time.ntp_servers[0] = load_string("ntp1");
  _time.ntp_servers[1] = load_string("ntp2");
  _time.ntp_servers[2] = load_string("ntp3");
  return ESP_OK;
}

bool AppConfig::admin_configured() const {
  std::lock_guard<std::mutex> guard(_mutex);
  return _admin_configured;
}

bool AppConfig::check_admin_password(const std::string &password) const {
  std::lock_guard<std::mutex> guard(_mutex);
  // First-run setup is intentionally unauthenticated until an admin password
  // exists; HTTPS handlers must only use this bypass for setup/provisioning.
  if (!_admin_configured)
    return true;
  uint8_t hash[32] = {};
  compute_password_hash(password, _password_salt, hash);
  uint8_t diff = 0;
  for (size_t i = 0; i < sizeof(hash); i++) diff |= hash[i] ^ _password_hash[i];
  return diff == 0;
}

esp_err_t AppConfig::set_admin_password(const std::string &password) {
  if (password.size() < 8)
    return ESP_ERR_INVALID_ARG;

  std::lock_guard<std::mutex> guard(_mutex);
  esp_fill_random(_password_salt, sizeof(_password_salt));
  compute_password_hash(password, _password_salt, _password_hash);
  _admin_configured = true;

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;
  err = nvs_set_blob(handle, "pwd_salt", _password_salt, sizeof(_password_salt));
  if (err == ESP_OK)
    err = nvs_set_blob(handle, "pwd_hash", _password_hash, sizeof(_password_hash));
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

NetworkConfig AppConfig::network() const {
  std::lock_guard<std::mutex> guard(_mutex);
  return _network;
}

esp_err_t AppConfig::set_network(const NetworkConfig &config) {
  std::lock_guard<std::mutex> guard(_mutex);
  _network = config;
  esp_err_t err = save_string("wifi_ssid", _network.ssid);
  if (err == ESP_OK) err = save_string("wifi_pass", _network.password);
  if (err == ESP_OK) err = save_u8("sta_static", _network.static_ip ? 1 : 0);
  if (err == ESP_OK) err = save_string("sta_ip", _network.ip);
  if (err == ESP_OK) err = save_string("sta_mask", _network.netmask);
  if (err == ESP_OK) err = save_string("sta_gw", _network.gateway);
  if (err == ESP_OK) err = save_string("dns1", _network.dns[0]);
  if (err == ESP_OK) err = save_string("dns2", _network.dns[1]);
  return err;
}

DisplayOptions AppConfig::display() const {
  std::lock_guard<std::mutex> guard(_mutex);
  return _display;
}

esp_err_t AppConfig::set_display(const DisplayOptions &options) {
  std::lock_guard<std::mutex> guard(_mutex);
  DisplayOptions next = options;
  next.layout = std::clamp<int>(next.layout, 0, 3);
  next.refresh_cadence_minutes = std::clamp<int>(next.refresh_cadence_minutes, kQuoteCadenceMin, kQuoteCadenceMax);
  next.timezone = std::clamp<int>(next.timezone, 0, 9);
  next.dst_mode = std::clamp<int>(next.dst_mode, 0, 2);
  next.clock_format = std::clamp<int>(next.clock_format, 0, 1);
  next.content_margin = std::clamp<int>(next.content_margin, 16, 72);
  if (!next.quote_time_specific_enabled && !next.quote_classics_enabled)
    next.quote_time_specific_enabled = true;
  next.highlight_time_color = std::clamp<int>(next.highlight_time_color, 0, 9);
  next.highlight_time_text_color = std::clamp<int>(next.highlight_time_text_color, 0, 9);
  next.main_pane_bg_color = std::clamp<int>(next.main_pane_bg_color, 0, 9);
  next.main_pane_text_color = std::clamp<int>(next.main_pane_text_color, 0, 9);
  next.sidebar_color = std::clamp<int>(next.sidebar_color, 0, 9);
  next.bottom_bar_bg_color = std::clamp<int>(next.bottom_bar_bg_color, 0, 9);
  next.bottom_bar_text_color = std::clamp<int>(next.bottom_bar_text_color, 0, 9);
  next.top_bar_bg_color = std::clamp<int>(next.top_bar_bg_color, 0, 9);
  next.top_bar_text_color = std::clamp<int>(next.top_bar_text_color, 0, 9);
  next.top_bar_date_format = std::clamp<int>(next.top_bar_date_format, 0, 5);

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_u8(handle, "disp_en", next.display_enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_i32(handle, "layout", next.layout);
  if (err == ESP_OK) err = nvs_set_i32(handle, "cadence", next.refresh_cadence_minutes);
  if (err == ESP_OK) err = nvs_set_i32(handle, "tz", next.timezone);
  if (err == ESP_OK) err = nvs_set_i32(handle, "dst", next.dst_mode);
  if (err == ESP_OK) err = nvs_set_i32(handle, "clock_fmt", next.clock_format);
  if (err == ESP_OK) err = nvs_set_i32(handle, "margin", next.content_margin);
  if (err == ESP_OK) err = nvs_set_u8(handle, "clock_vis", next.clock_visible ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(handle, "quote_vis", next.quote_visible ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(handle, "quote_time", next.quote_time_specific_enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(handle, "quote_class", next.quote_classics_enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(handle, "hl_time", next.highlight_time_enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_i32(handle, "hl_col", next.highlight_time_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "hl_txt", next.highlight_time_text_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "main_bg", next.main_pane_bg_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "main_txt", next.main_pane_text_color);
  if (err == ESP_OK) err = nvs_set_u8(handle, "side_vis", next.sidebar_visible ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_i32(handle, "side_col", next.sidebar_color);
  if (err == ESP_OK) err = nvs_set_u8(handle, "bot_vis", next.bottom_bar_visible ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_i32(handle, "bot_bg", next.bottom_bar_bg_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "bot_txt", next.bottom_bar_text_color);
  if (err == ESP_OK) err = nvs_set_u8(handle, "top_vis", next.top_bar_visible ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_i32(handle, "top_bg", next.top_bar_bg_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "top_txt", next.top_bar_text_color);
  if (err == ESP_OK) err = nvs_set_i32(handle, "top_date", next.top_bar_date_format);
  if (err == ESP_OK) err = nvs_set_u8(handle, "watch_style", next.watch_style ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK)
    _display = next;
  return err;
}

TimeConfig AppConfig::time() const {
  std::lock_guard<std::mutex> guard(_mutex);
  return _time;
}

esp_err_t AppConfig::set_time(const TimeConfig &config) {
  std::lock_guard<std::mutex> guard(_mutex);
  _time = config;
  esp_err_t err = save_i32("ntp_mode", static_cast<int>(_time.ntp_mode));
  if (err == ESP_OK) err = save_string("ntp1", _time.ntp_servers[0]);
  if (err == ESP_OK) err = save_string("ntp2", _time.ntp_servers[1]);
  if (err == ESP_OK) err = save_string("ntp3", _time.ntp_servers[2]);
  return err;
}

esp_err_t AppConfig::save_string(const char *key, const std::string &value) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, key, value.c_str());
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
  }
  return err;
}

esp_err_t AppConfig::save_u8(const char *key, uint8_t value) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
  }
  return err;
}

esp_err_t AppConfig::save_i32(const char *key, int32_t value) {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
  }
  return err;
}

std::string AppConfig::load_string(const char *key, const char *fallback) const {
  nvs_handle_t handle = 0;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK)
    return fallback;

  size_t len = 0;
  if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK || len == 0) {
    nvs_close(handle);
    return fallback;
  }

  std::string value;
  value.resize(len);
  if (nvs_get_str(handle, key, value.data(), &len) == ESP_OK && !value.empty() && value.back() == '\0')
    value.pop_back();
  else
    value = fallback;
  nvs_close(handle);
  return value;
}

bool AppConfig::load_bool(const char *key, bool fallback) const {
  nvs_handle_t handle = 0;
  bool value = fallback;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
    uint8_t value_u8 = fallback ? 1 : 0;
    esp_err_t err = nvs_get_u8(handle, key, &value_u8);
    if (err == ESP_OK) {
      value = value_u8 != 0;
    } else {
      int32_t value_i32 = fallback ? 1 : 0;
      if (nvs_get_i32(handle, key, &value_i32) == ESP_OK)
        value = value_i32 != 0;
    }
    nvs_close(handle);
  }
  return value;
}

uint8_t AppConfig::load_u8(const char *key, uint8_t fallback) const {
  nvs_handle_t handle = 0;
  uint8_t value = fallback;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
    (void)nvs_get_u8(handle, key, &value);
    nvs_close(handle);
  }
  return value;
}

int32_t AppConfig::load_i32(const char *key, int32_t fallback) const {
  nvs_handle_t handle = 0;
  int32_t value = fallback;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
    (void)nvs_get_i32(handle, key, &value);
    nvs_close(handle);
  }
  return value;
}

void AppConfig::compute_password_hash(const std::string &password, const uint8_t salt[16], uint8_t hash[32]) const {
  std::vector<unsigned char> input;
  input.reserve(16 + password.size());
  input.insert(input.end(), salt, salt + 16);
  input.insert(input.end(), password.begin(), password.end());
  esp_sha(SHA2_256, input.data(), input.size(), hash);
}

}  // namespace quotes_clock
