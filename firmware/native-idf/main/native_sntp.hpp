#pragma once

#include <array>
#include <atomic>
#include <string>

#include <esp_err.h>
#include <esp_event.h>

#include "app_config.hpp"

namespace quotes_clock {

struct SntpStatus {
  bool enabled = false;
  int mode = 0;
  int sync_status = 0;
  uint32_t last_sync_age_ms = 0;
  std::array<std::string, 3> servers;
};

class NativeSntp {
 public:
  NativeSntp(const NativeSntp &) = delete;

  static NativeSntp &instance();

  esp_err_t start();
  esp_err_t apply_config(const TimeConfig &config);
  SntpStatus status() const;

 private:
  NativeSntp() = default;

  static void sync_cb(struct timeval *tv);
  static void ip_event_thunk(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  void update();

  TimeConfig _config;
  std::atomic<bool> _started{false};
  std::atomic<uint32_t> _synced_at_ms{0};
};

}  // namespace quotes_clock
