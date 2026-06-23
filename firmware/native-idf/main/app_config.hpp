#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include <esp_err.h>

#include "native_display.hpp"

namespace quotes_clock {

enum class NtpMode : uint8_t {
  DefaultPool = 0,
  Dhcp = 1,
  Manual = 2,
};

struct NetworkConfig {
  std::string ssid;
  std::string password;
  bool static_ip = false;
  std::string ip;
  std::string netmask;
  std::string gateway;
  std::array<std::string, 2> dns;
};

struct TimeConfig {
  NtpMode ntp_mode = NtpMode::DefaultPool;
  std::array<std::string, 3> ntp_servers;
};

class AppConfig {
 public:
  AppConfig(const AppConfig &) = delete;

  static AppConfig &instance();

  esp_err_t load();
  bool admin_configured() const;
  bool check_admin_password(const std::string &password) const;
  esp_err_t set_admin_password(const std::string &password);

  NetworkConfig network() const;
  esp_err_t set_network(const NetworkConfig &config);

  DisplayOptions display() const;
  esp_err_t set_display(const DisplayOptions &options);

  TimeConfig time() const;
  esp_err_t set_time(const TimeConfig &config);

 private:
  AppConfig() = default;

  esp_err_t save_string(const char *key, const std::string &value);
  esp_err_t save_u8(const char *key, uint8_t value);
  esp_err_t save_i32(const char *key, int32_t value);
  std::string load_string(const char *key, const char *fallback = "") const;
  bool load_bool(const char *key, bool fallback) const;
  uint8_t load_u8(const char *key, uint8_t fallback) const;
  int32_t load_i32(const char *key, int32_t fallback) const;
  void compute_password_hash(const std::string &password, const uint8_t salt[16], uint8_t hash[32]) const;

  mutable std::mutex _mutex;
  bool _admin_configured = false;
  uint8_t _password_salt[16] = {};
  uint8_t _password_hash[32] = {};
  NetworkConfig _network;
  DisplayOptions _display;
  TimeConfig _time;
};

}  // namespace quotes_clock
