#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace quotes_clock::assets {
struct Font;
}

namespace quotes_clock {

struct DisplayOptions {
  bool display_enabled = true;
  int layout = 1;
  int refresh_cadence_minutes = 1;
  int timezone = 1;
  int dst_mode = 1;
  int clock_format = 0;
  int content_margin = 16;
  bool clock_visible = false;
  bool quote_visible = true;
  bool quote_time_specific_enabled = true;
  bool quote_classics_enabled = false;
  bool highlight_time_enabled = true;
  int highlight_time_color = 0;
  int highlight_time_text_color = 8;
  int main_pane_bg_color = 9;
  int main_pane_text_color = 8;
  bool sidebar_visible = true;
  int sidebar_color = 1;
  bool bottom_bar_visible = true;
  int bottom_bar_bg_color = 0;
  int bottom_bar_text_color = 8;
  bool top_bar_visible = true;
  int top_bar_bg_color = 8;
  int top_bar_text_color = 0;
  int top_bar_date_format = 0;
  bool watch_style = false;
};

struct DisplayStatus {
  bool initialized = false;
  bool busy = false;
  uint32_t refreshes = 0;
  uint32_t last_refresh_ms = 0;
  uint32_t last_render_ms = 0;
  uint32_t last_init_ms = 0;
  uint32_t last_transfer_ms = 0;
  uint32_t last_panel_refresh_ms = 0;
  uint32_t last_sleep_ms = 0;
  esp_err_t last_error = ESP_OK;
  bool last_refresh_partial = false;
  uint32_t last_partial_bytes = 0;
  int last_partial_x_start = -1;
  int last_partial_x_end = -1;
  int last_partial_y_start = -1;
  int last_partial_y_end = -1;
  bool quote_pack_ready = false;
  uint16_t quote_count = 0;
  uint16_t time_specific_quote_count = 0;
  uint16_t classic_quote_count = 0;
  uint32_t quote_pack_size = 0;
};

class NativeDisplay {
 public:
  NativeDisplay(const NativeDisplay &) = delete;

  static NativeDisplay &instance();

  esp_err_t start();
  void request_refresh(bool fast);
  DisplayOptions options() const;
  void set_options(const DisplayOptions &options);
  DisplayStatus status() const;

 private:
  static constexpr int kDefaultTimezone = 1;
  static constexpr int kDstOff = 0;
  static constexpr int kDstAuto = 1;
  static constexpr int kDstOn = 2;
  static constexpr int kClock24Hour = 0;
  static constexpr int kClock12Hour = 1;
  static constexpr int kDefaultContentMargin = 16;

  enum class Color : uint8_t {
    Black = 0b00,
    White = 0b01,
    Yellow = 0b10,
    Red = 0b11,
  };

  enum class TextAlign : uint8_t {
    Left,
    Right,
  };

  enum class WatchdogReason : int {
    None = 0,
    TaskStale = 1,
    RefreshStale = 2,
    RefreshOverdue = 3,
  };

  struct TimeState {
    bool valid = false;
    tm local{};
    int minute = 0;
    int minute_key = -1;
    int day_key = -1;
  };

  struct LogicalRect {
    int x_start = 0;
    int y_start = 0;
    int x_end = 0;
    int y_end = 0;
  };

  struct PartialWindow {
    uint16_t x_start = 0;
    uint16_t x_end = 0;
    uint16_t y_start = 0;
    uint16_t y_end = 0;
    uint16_t byte_start = 0;
    uint16_t row_bytes = 0;
    uint16_t rows = 0;
    uint32_t transfer_bytes = 0;
  };

  struct DecodedQuote {
    std::string text;
    std::string title;
    std::string author;
    uint16_t highlight_offset = 0;
    uint16_t highlight_length = 0;
    bool time_specific = true;
  };

  struct WrappedLine {
    std::string text;
    size_t source_offset = 0;
    size_t source_length = 0;
  };

  struct WatchLayout {
    int pane_x = 0;
    int pane_y = 0;
    int pane_w = 0;
    int pane_h = 0;
    int scale = 1;
    int digit_w = 0;
    int digit_h = 0;
    int digit_gap = 0;
    int colon_gap = 0;
    int colon_w = 0;
    int digit_x = 0;
    int digit_y = 0;
    int day_center_x = 0;
    int day_y = 0;
    int day_scale = 1;
    int mode_x = 0;
    int mode_y = 0;
    int date_right_x = 0;
    int date_y = 0;
    int small_scale = 1;
  };

  static constexpr int kWidth = 800;
  static constexpr int kHeight = 480;
  static constexpr int kPixelsPerByte = 4;
  static constexpr int kRowBytes = kWidth / kPixelsPerByte;
  static constexpr int kBufferBytes = kRowBytes * kHeight;
  static constexpr int kFooterHeight = 16;
  static constexpr size_t kTransferChunkBytes = 4096;

  static constexpr gpio_num_t kPinSck = GPIO_NUM_18;
  static constexpr gpio_num_t kPinMosi = GPIO_NUM_23;
  static constexpr gpio_num_t kPinBusy = GPIO_NUM_13;
  static constexpr gpio_num_t kPinReset = GPIO_NUM_12;
  static constexpr gpio_num_t kPinDc = GPIO_NUM_14;
  static constexpr gpio_num_t kPinCs = GPIO_NUM_27;
  static constexpr int kSpiClockHz = 12 * 1000 * 1000;
  static constexpr bool kUseBitBangSpi = false;
  static constexpr uint32_t kWatchdogIntervalMs = 5000;
  static constexpr uint32_t kTaskStaleMs = 120000;
  static constexpr uint32_t kRefreshStaleMs = 180000;
  static constexpr uint32_t kOverdueRefreshSlackMs = 120000;

  NativeDisplay();

  static void task_thunk(void *arg);
  static void watchdog_task_thunk(void *arg);
  void task();
  void watchdog_task();
  void watchdog_restart(WatchdogReason reason, uint32_t age_ms);
  bool should_schedule_refresh();
  bool pane_refresh_due(const TimeState &time) const;
  bool can_use_partial_refresh(bool requested) const;
  int frame_kind() const;
  void apply_timezone() const;
  const char *timezone_spec() const;

  esp_err_t setup();
  esp_err_t setup_gpio();
  esp_err_t setup_spi();
  esp_err_t hard_reset();
  esp_err_t init_panel(bool partial);
  esp_err_t transfer_frame(const PartialWindow *window);
  esp_err_t transfer_partial_window(const PartialWindow &window);
  esp_err_t refresh_panel(const PartialWindow *window);
  esp_err_t sleep_panel(bool partial);
  esp_err_t write_partial_window(const PartialWindow &window);
  esp_err_t transmit(const uint8_t *values, size_t length);
  esp_err_t command(uint8_t value);
  esp_err_t data(const uint8_t *values, size_t length);
  esp_err_t data(uint8_t value);
  esp_err_t cmd_data(uint8_t cmd, const uint8_t *values, size_t length);
  esp_err_t cmd_data(uint8_t cmd, uint8_t value);
  esp_err_t wait_idle(uint32_t timeout_ms);
  esp_err_t wait_busy(uint32_t timeout_ms);
  esp_err_t wait_busy_then_idle(uint32_t busy_timeout_ms, uint32_t idle_timeout_ms);

  void render_clock_frame();
  void render_clock_tick(const TimeState &time, const LogicalRect &dirty_rect);
  TimeState current_time() const;
  bool quote_for_time(const TimeState &time, DecodedQuote &quote) const;
  int header_height() const;
  int footer_height() const;
  int logical_width() const;
  int logical_height() const;
  bool portrait_layout() const;
  bool watch_style_active() const;
  int wifi_signal_bars() const;
  bool show_setup_instructions() const;
  void draw_clock_text(const TimeState &time, int x, int y, const assets::Font &clock_font,
                       const assets::Font &suffix_font, int text_color);
  void draw_clock_only(const TimeState &time, int content_margin, int text_color);
  std::string clock_text_for(const TimeState &time) const;
  LogicalRect clock_rect_for() const;
  LogicalRect clock_dirty_rect_for(const TimeState &time) const;
  WatchLayout watch_layout() const;
  LogicalRect watch_dirty_rect_for(const TimeState &time) const;
  LogicalRect main_pane_rect() const;
  PartialWindow partial_window_for(const LogicalRect &rect) const;
  bool logical_to_physical(int &x, int &y) const;
  void draw_logo(int origin_x, int origin_y);
  void draw_wifi_status_icon(int center_x, int center_y, int display_color);
  void draw_watch_style_frame(const TimeState &time);
  void draw_watch_digit(int x, int y, int scale, int digit, int display_color);
  void draw_watch_symbol(int x, int y, int scale, char symbol, int display_color);
  void draw_watch_day_label(int center_x, int y, int scale, const char *text, int display_color);
  void draw_watch_day_symbol(int x, int y, int scale, char symbol, bool first_letter, int display_color);
  void draw_watch_colon(int x, int y, int scale, int display_color);
  void draw_watch_segment(int x, int y, int scale, int segment, int display_color);
  void draw_watch_mode_segment(int x, int y, int scale, int segment, int display_color);
  void draw_watch_polygon(const int points[][2], int count, int display_color);
  void stroke_rect(int x, int y, int width, int height, int thickness, Color color);
  void draw_text_centered(int center_x, int y, const assets::Font &font, int display_color, const char *text);
  void draw_text(int x, int y, const assets::Font &font, Color color, const char *text,
                 TextAlign align = TextAlign::Left);
  void draw_text_display_color(int x, int y, const assets::Font &font, int display_color, const char *text,
                               TextAlign align = TextAlign::Left);
  int draw_wrapped_text(int x, int y, int max_width, int line_height, int max_lines, const assets::Font &font,
                        Color color, const char *text, TextAlign align = TextAlign::Left);
  int text_width(const assets::Font &font, const char *text) const;
  int text_visual_bottom(const assets::Font &font, const char *text) const;
  int wrapped_block_height(const std::vector<WrappedLine> &lines, const assets::Font &font, int line_height) const;
  std::vector<WrappedLine> wrap_text(const char *raw, const assets::Font &font, int max_width, int max_lines,
                                     bool &fitted) const;
  std::string ellipsize(std::string text, const assets::Font &font, int max_width) const;
  void draw_quote_line(int x, int y, int line_height, const assets::Font &font, const WrappedLine &line,
                       bool highlight_enabled, uint16_t highlight_offset, uint16_t highlight_length,
                       int highlight_color, int highlight_text_color, int base_text_color);
  void highlight_rect(int x, int y, int width, int height, int highlight_color);
  Color display_color_pixel(int display_color, int x, int y) const;
  void fill(Color color);
  void rect(int x, int y, int width, int height, Color color);
  void rect_display_color(int x, int y, int width, int height, int display_color);
  void logical_pixel(int x, int y, Color color);
  void pixel(int x, int y, Color color);

  spi_device_handle_t _spi = nullptr;
  TaskHandle_t _task = nullptr;
  TaskHandle_t _watchdog_task = nullptr;
  std::atomic<bool> _tasks_started{false};
  std::array<uint8_t, kBufferBytes> _buffer{};
  std::atomic<bool> _initialized{false};
  std::atomic<bool> _busy{false};
  std::atomic<bool> _refresh_pending{false};
  std::atomic<bool> _fast_pending{false};
  std::atomic<bool> _display_enabled{true};
  std::atomic<int> _layout_mode{1};
  std::atomic<int> _refresh_cadence_minutes{1};
  std::atomic<int> _timezone{kDefaultTimezone};
  std::atomic<int> _dst_mode{kDstAuto};
  std::atomic<int> _clock_format{kClock24Hour};
  std::atomic<int> _content_margin{kDefaultContentMargin};
  std::atomic<bool> _clock_visible{false};
  std::atomic<bool> _quote_visible{true};
  std::atomic<bool> _quote_time_specific_enabled{true};
  std::atomic<bool> _quote_classics_enabled{false};
  std::atomic<bool> _highlight_time_enabled{true};
  std::atomic<int> _highlight_time_color{0};
  std::atomic<int> _highlight_time_text_color{8};
  std::atomic<int> _main_pane_bg_color{9};
  std::atomic<int> _main_pane_text_color{8};
  std::atomic<bool> _sidebar_visible{true};
  std::atomic<int> _sidebar_color{1};
  std::atomic<bool> _bottom_bar_visible{true};
  std::atomic<int> _bottom_bar_bg_color{0};
  std::atomic<int> _bottom_bar_text_color{8};
  std::atomic<bool> _top_bar_visible{true};
  std::atomic<int> _top_bar_bg_color{8};
  std::atomic<int> _top_bar_text_color{0};
  std::atomic<int> _top_bar_date_format{0};
  std::atomic<bool> _watch_style{false};
  std::atomic<int> _last_rendered_minute_key{-1};
  std::atomic<int> _last_pane_refresh_minute_key{-1};
  std::atomic<int> _last_rendered_layout{-1};
  std::atomic<int> _last_rendered_frame_kind{-1};
  std::atomic<int> _last_rendered_wifi_state{-1};
  std::string _last_rendered_station_ssid;
  std::string _last_rendered_clock_text;
  std::atomic<int> _last_full_refresh_day_key{-1};
  std::atomic<uint32_t> _refresh_count{0};
  std::atomic<uint32_t> _last_refresh_ms{0};
  std::atomic<uint32_t> _last_render_ms{0};
  std::atomic<uint32_t> _last_init_ms{0};
  std::atomic<uint32_t> _last_transfer_ms{0};
  std::atomic<uint32_t> _last_panel_refresh_ms{0};
  std::atomic<uint32_t> _last_sleep_ms{0};
  std::atomic<bool> _last_refresh_partial{false};
  std::atomic<uint32_t> _last_partial_bytes{0};
  std::atomic<int> _last_partial_x_start{-1};
  std::atomic<int> _last_partial_x_end{-1};
  std::atomic<int> _last_partial_y_start{-1};
  std::atomic<int> _last_partial_y_end{-1};
  std::atomic<bool> _controller_standby{false};
  std::atomic<int> _last_error{ESP_OK};
  std::atomic<int> _last_stage{0};
  std::atomic<uint32_t> _task_heartbeat_ms{0};
  std::atomic<uint32_t> _refresh_started_ms{0};
  std::atomic<uint32_t> _last_refresh_finished_ms{0};
  std::atomic<uint32_t> _watchdog_recoveries{0};
  std::atomic<int> _watchdog_last_reason{static_cast<int>(WatchdogReason::None)};
  int _render_layout_mode = 1;
};

}  // namespace quotes_clock
