#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

namespace quotes_clock {

class NativeHttpsServer {
 public:
  NativeHttpsServer(const NativeHttpsServer &) = delete;

  static NativeHttpsServer &instance();

  esp_err_t start();

 private:
  NativeHttpsServer() = default;

  httpd_handle_t _server = nullptr;

  esp_err_t send_index(httpd_req_t *req);
  esp_err_t send_favicon(httpd_req_t *req);
  esp_err_t send_status(httpd_req_t *req);
  esp_err_t send_config(httpd_req_t *req);
  esp_err_t set_admin_password(httpd_req_t *req);
  esp_err_t set_wifi(httpd_req_t *req);
  esp_err_t set_display(httpd_req_t *req);
  esp_err_t set_time(httpd_req_t *req);
  esp_err_t force_refresh(httpd_req_t *req);
  esp_err_t clear_wifi(httpd_req_t *req);
  esp_err_t ota_upload(httpd_req_t *req);
  bool authenticate(httpd_req_t *req);

  static esp_err_t index_handler(httpd_req_t *req);
  static esp_err_t favicon_handler(httpd_req_t *req);
  static esp_err_t status_handler(httpd_req_t *req);
  static esp_err_t config_handler(httpd_req_t *req);
  static esp_err_t admin_password_handler(httpd_req_t *req);
  static esp_err_t wifi_handler(httpd_req_t *req);
  static esp_err_t display_handler(httpd_req_t *req);
  static esp_err_t time_handler(httpd_req_t *req);
  static esp_err_t refresh_handler(httpd_req_t *req);
  static esp_err_t clear_wifi_handler(httpd_req_t *req);
  static esp_err_t ota_handler(httpd_req_t *req);
};

}  // namespace quotes_clock
