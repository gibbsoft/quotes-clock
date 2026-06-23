#include "native_display.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "generated/quotes_clock_assets.hpp"
#include "platform_state.hpp"

namespace quotes_clock {
namespace {
constexpr const char *kTag = "native_display";

constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWEROFF = 0x02;
constexpr uint8_t CMD_POWERON = 0x04;
constexpr uint8_t CMD_DEEPSLEEP = 0x07;
constexpr uint8_t CMD_TRANSFER = 0x10;
constexpr uint8_t CMD_REFRESH = 0x12;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x83;
constexpr uint8_t PSR_ACTIVE = 0x0F;
constexpr uint8_t PSR_STANDBY = 0x0D;
constexpr uint8_t PSR_FLAGS = 0x29;
constexpr uint32_t WATCHDOG_RTC_MAGIC = 0x51435744;  // QCWD
constexpr uint32_t BOOT_STATUS_SCREEN_DELAY_MS = 10000;
constexpr uint32_t SETUP_SCREEN_GRACE_MS = 90000;
constexpr const char *QUOTE_PARTITION_LABEL = "quote_data";
constexpr int kHighlightYellow = 0;
constexpr int kHighlightRed = 1;
constexpr int kHighlightGrey = 2;
constexpr int kHighlightLightRed = 3;
constexpr int kHighlightDarkRed = 4;
constexpr int kHighlightLightYellow = 5;
constexpr int kHighlightDarkYellow = 6;
constexpr int kHighlightOrange = 7;
constexpr int kHighlightBlack = 8;
constexpr int kHighlightWhite = 9;
constexpr int kDateFormatAuto = 0;
constexpr int kDateFormatMax = 5;
constexpr int kQuoteCadenceMin = 1;
constexpr int kQuoteCadenceMax = 1440;
constexpr int kWatchDigitUnits = 20;
constexpr int kWatchDigitHeightUnits = 36;
constexpr int kWatchDigitGapUnits = 6;
constexpr int kWatchColonGapUnits = 5;
constexpr int kWatchColonUnits = 4;
constexpr int kWatchTimeUnits =
    kWatchDigitUnits * 4 + kWatchDigitGapUnits * 2 + kWatchColonGapUnits * 2 + kWatchColonUnits;

// Digital segment geometry is derived from the MIT-licensed casio-f91w-fsm
// emulator SVG by Jakub Dundalek and Alexis Philip. Coordinates are normalized
// to tenths of a 20x36 unit digit.
struct WatchSegmentShape {
  const int16_t (*points)[2] = nullptr;
  int count = 0;
};

static constexpr int16_t kWatchSegA[][2] = {
    {165, 0}, {51, 0}, {40, 2}, {33, 6}, {33, 11}, {64, 42}, {72, 46}, {137, 46},
    {144, 44}, {148, 40}, {171, 5}, {170, 1}, {165, 0},
};
static constexpr int16_t kWatchSegB[][2] = {
    {148, 140}, {151, 147}, {177, 162}, {185, 161}, {194, 149}, {200, 30},
    {199, 24}, {187, 10}, {182, 9}, {153, 52}, {151, 57},
};
static constexpr int16_t kWatchSegC[][2] = {
    {191, 204}, {181, 323}, {177, 335}, {165, 351}, {160, 354}, {156, 353},
    {136, 313}, {134, 305}, {141, 201}, {167, 183}, {176, 181}, {184, 185},
    {191, 203},
};
static constexpr int16_t kWatchSegD[][2] = {
    {123, 316}, {60, 316}, {49, 320}, {12, 341}, {8, 345}, {9, 349}, {23, 358},
    {32, 360}, {143, 359}, {144, 353}, {130, 321}, {124, 316},
};
static constexpr int16_t kWatchSegE[][2] = {
    {51, 204}, {46, 305}, {43, 309}, {9, 328}, {3, 329}, {0, 326}, {6, 195},
    {13, 185}, {22, 183}, {48, 198}, {51, 204},
};
static constexpr int16_t kWatchSegF[][2] = {
    {58, 138}, {52, 146}, {23, 165}, {19, 165}, {13, 163}, {9, 151}, {16, 38},
    {19, 28}, {25, 20}, {32, 20}, {59, 49}, {63, 60},
};
static constexpr int16_t kWatchSegG[][2] = {
    {35, 170}, {68, 152}, {137, 154}, {143, 155}, {164, 171}, {166, 174},
    {164, 176}, {137, 190}, {130, 192}, {68, 193}, {57, 190}, {35, 177},
    {33, 173}, {35, 170},
};
static constexpr int16_t kWatchModeSegH[][2] = {
    {86, 64}, {124, 64}, {120, 138}, {101, 153}, {82, 138}, {86, 64},
};
static constexpr int16_t kWatchModeSegI[][2] = {
    {113, 296}, {75, 296}, {78, 212}, {97, 197}, {116, 212}, {113, 296},
};

#define QC_WATCH_SEGMENT_SHAPE(shape) \
  { shape, static_cast<int>(sizeof(shape) / sizeof((shape)[0])) }

static constexpr WatchSegmentShape kWatchSegments[] = {
    QC_WATCH_SEGMENT_SHAPE(kWatchSegA), QC_WATCH_SEGMENT_SHAPE(kWatchSegB),
    QC_WATCH_SEGMENT_SHAPE(kWatchSegC), QC_WATCH_SEGMENT_SHAPE(kWatchSegD),
    QC_WATCH_SEGMENT_SHAPE(kWatchSegE), QC_WATCH_SEGMENT_SHAPE(kWatchSegF),
    QC_WATCH_SEGMENT_SHAPE(kWatchSegG),
};

static constexpr WatchSegmentShape kWatchModeSegments[] = {
    QC_WATCH_SEGMENT_SHAPE(kWatchModeSegH),
    QC_WATCH_SEGMENT_SHAPE(kWatchModeSegI),
};

#undef QC_WATCH_SEGMENT_SHAPE

constexpr uint8_t INIT_SEQUENCE[][10] = {
    {CMD_PANEL_SETTING, 2, PSR_ACTIVE, PSR_FLAGS},
    {0x06, 4, 0x0F, 0x8B, 0x93, 0xA1},
    {0x41, 1, 0x00},
    {0x50, 1, 0x37},
    {0x60, 2, 0x02, 0x02},
    {0x61, 4, 800 / 256, 800 % 256, 480 / 256, 480 % 256},
    {0x62, 8, 0x98, 0x98, 0x98, 0x75, 0xCA, 0xB2, 0x98, 0x7E},
    {0x65, 4, 0x00, 0x00, 0x00, 0x00},
    {0xE7, 1, 0x1C},
    {0xE3, 1, 0x00},
    {0xE9, 1, 0x01},
    {0x30, 1, 0x08},
};

RTC_NOINIT_ATTR uint32_t g_watchdog_rtc_magic;
RTC_NOINIT_ATTR uint32_t g_watchdog_rtc_recoveries;
RTC_NOINIT_ATTR uint32_t g_watchdog_rtc_last_reason;
portMUX_TYPE g_watchdog_rtc_lock = portMUX_INITIALIZER_UNLOCKED;

#define QC_RETURN_ON_ERROR(expr)        \
  do {                                  \
    const esp_err_t _err = (expr);      \
    if (_err != ESP_OK) return _err;    \
  } while (false)

uint32_t millis() {
  // Intentionally wraps after about 49 days; watchdog age checks use unsigned
  // subtraction so wrap-around remains safe.
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void sleep_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

struct __attribute__((packed)) QuotePackHeader {
  char magic[4];
  uint16_t version;
  uint16_t header_size;
  uint16_t quote_count;
  uint16_t token_count;
  uint8_t token_marker;
  uint8_t reserved[3];
  uint32_t records_offset;
  uint32_t tokens_offset;
  uint32_t dictionary_offset;
  uint32_t dictionary_size;
  uint32_t quote_data_offset;
  uint32_t quote_data_size;
  uint32_t total_size;
};

struct __attribute__((packed)) QuotePackRecord {
  uint16_t minute;
  uint8_t category;
  uint8_t reserved;
  uint32_t text_offset;
  uint16_t text_length;
  uint32_t title_offset;
  uint16_t title_length;
  uint32_t author_offset;
  uint16_t author_length;
  uint16_t highlight_offset;
  uint16_t highlight_length;
};

struct __attribute__((packed)) QuotePackToken {
  uint16_t offset;
  uint8_t length;
};

static_assert(sizeof(QuotePackHeader) == 44);
static_assert(sizeof(QuotePackRecord) == 26);
static_assert(sizeof(QuotePackToken) == 3);

constexpr uint8_t kQuoteCategoryTimeSpecific = 0;
constexpr uint8_t kQuoteCategoryClassic = 1;

class QuotePartitionStore {
 public:
  bool decode_minute(int minute, std::string &text, std::string &title, std::string &author,
                     uint16_t &highlight_offset, uint16_t &highlight_length) {
    if (!load())
      return false;
    for (uint16_t i = 0; i < _header->quote_count; i++) {
      const QuotePackRecord &record = _records[i];
      if (record.category == kQuoteCategoryTimeSpecific && record.minute == minute) {
        decode_record(record, text, title, author, highlight_offset, highlight_length);
        return true;
      }
    }
    return false;
  }

  bool decode_classic(uint32_t selector, std::string &text, std::string &title, std::string &author,
                      uint16_t &highlight_offset, uint16_t &highlight_length) {
    if (!load() || _classic_count == 0)
      return false;
    const uint16_t target = selector % _classic_count;
    uint16_t seen = 0;
    for (uint16_t i = 0; i < _header->quote_count; i++) {
      const QuotePackRecord &record = _records[i];
      if (record.category != kQuoteCategoryClassic)
        continue;
      if (seen++ == target) {
        decode_record(record, text, title, author, highlight_offset, highlight_length);
        highlight_offset = 0;
        highlight_length = 0;
        return true;
      }
    }
    return false;
  }

  bool ready() {
    return load();
  }

  uint16_t quote_count() {
    return load() ? _header->quote_count : 0;
  }

  uint16_t time_specific_count() {
    return load() ? _time_specific_count : 0;
  }

  uint16_t classic_count() {
    return load() ? _classic_count : 0;
  }

  uint32_t total_size() {
    return load() ? _header->total_size : 0;
  }

 private:
  bool load() {
    if (_checked)
      return _ready;
    _checked = true;

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, QUOTE_PARTITION_LABEL);
    if (partition == nullptr) {
      ESP_LOGW(kTag, "quote partition '%s' not found", QUOTE_PARTITION_LABEL);
      return false;
    }

    const void *mapped = nullptr;
    const esp_err_t err =
        esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &mapped, &_mmap_handle);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "unable to mmap quote partition: %s", esp_err_to_name(err));
      return false;
    }

    _base = static_cast<const uint8_t *>(mapped);
    _partition_size = partition->size;
    if (!validate()) {
      ESP_LOGW(kTag, "quote partition is missing or has an unsupported format");
      return false;
    }

    _ready = true;
    ESP_LOGI(kTag, "mapped quote partition: %u quotes, %lu bytes", _header->quote_count,
             static_cast<unsigned long>(_header->total_size));
    return true;
  }

  bool validate_range(uint32_t offset, uint32_t size) const {
    return offset <= _partition_size && size <= _partition_size - offset;
  }

  bool validate() {
    if (_partition_size < sizeof(QuotePackHeader))
      return false;
    _header = reinterpret_cast<const QuotePackHeader *>(_base);
    if (std::memcmp(_header->magic, "QCQ3", 4) != 0 || _header->version != 3 ||
        _header->header_size != sizeof(QuotePackHeader) || _header->quote_count == 0 ||
        _header->quote_count > 8192 || _header->token_count > 256 || _header->token_marker != 0xFF ||
        _header->total_size < sizeof(QuotePackHeader) || _header->total_size > _partition_size) {
      return false;
    }

    const uint32_t records_size = static_cast<uint32_t>(_header->quote_count) * sizeof(QuotePackRecord);
    const uint32_t tokens_size = static_cast<uint32_t>(_header->token_count) * sizeof(QuotePackToken);
    if (!validate_range(_header->records_offset, records_size) || !validate_range(_header->tokens_offset, tokens_size) ||
        !validate_range(_header->dictionary_offset, _header->dictionary_size) ||
        !validate_range(_header->quote_data_offset, _header->quote_data_size)) {
      return false;
    }
    if (_header->records_offset + records_size > _header->total_size ||
        _header->tokens_offset + tokens_size > _header->total_size ||
        _header->dictionary_offset + _header->dictionary_size > _header->total_size ||
        _header->quote_data_offset + _header->quote_data_size > _header->total_size) {
      return false;
    }

    _records = reinterpret_cast<const QuotePackRecord *>(_base + _header->records_offset);
    _tokens = reinterpret_cast<const QuotePackToken *>(_base + _header->tokens_offset);
    _dictionary = _base + _header->dictionary_offset;
    _quote_data = _base + _header->quote_data_offset;
    _time_specific_count = 0;
    _classic_count = 0;
    for (uint16_t i = 0; i < _header->quote_count; i++) {
      if (_records[i].category == kQuoteCategoryTimeSpecific)
        _time_specific_count++;
      else if (_records[i].category == kQuoteCategoryClassic)
        _classic_count++;
    }
    return true;
  }

  void decode_record(const QuotePackRecord &record, std::string &text, std::string &title, std::string &author,
                     uint16_t &highlight_offset, uint16_t &highlight_length) const {
    decode_field(record.text_offset, record.text_length, text);
    decode_field(record.title_offset, record.title_length, title);
    decode_field(record.author_offset, record.author_length, author);
    highlight_offset = record.highlight_offset;
    highlight_length = record.highlight_length;
  }

  void decode_field(uint32_t offset, uint16_t length, std::string &out) const {
    out.clear();
    const uint32_t end = std::min<uint32_t>(offset + length, _header->quote_data_size);
    for (uint32_t index = offset; index < end; index++) {
      const uint8_t value = _quote_data[index];
      if (value == _header->token_marker && index + 1 < end) {
        const uint8_t token_index = _quote_data[++index];
        if (token_index < _header->token_count) {
          const QuotePackToken &token = _tokens[token_index];
          if (static_cast<uint32_t>(token.offset) + token.length <= _header->dictionary_size) {
            out.append(reinterpret_cast<const char *>(_dictionary + token.offset), token.length);
          }
        }
      } else {
        out.push_back(static_cast<char>(value));
      }
    }
  }

  bool _checked = false;
  bool _ready = false;
  const uint8_t *_base = nullptr;
  size_t _partition_size = 0;
  esp_partition_mmap_handle_t _mmap_handle = 0;
  const QuotePackHeader *_header = nullptr;
  const QuotePackRecord *_records = nullptr;
  const QuotePackToken *_tokens = nullptr;
  const uint8_t *_dictionary = nullptr;
  const uint8_t *_quote_data = nullptr;
  uint16_t _time_specific_count = 0;
  uint16_t _classic_count = 0;
};

QuotePartitionStore &quote_partition_store() {
  static QuotePartitionStore store;
  return store;
}

const char *stage_name(int stage) {
  switch (stage) {
    case 1:
      return "render";
    case 2:
      return "init";
    case 3:
      return "transfer";
    case 4:
      return "refresh";
    case 5:
      return "sleep";
    case 6:
      return "done";
    default:
      return "idle";
  }
}

const char *watchdog_reason_name(int reason) {
  switch (reason) {
    case 1:
      return "task-stale";
    case 2:
      return "refresh-stale";
    case 3:
      return "refresh-overdue";
    default:
      return "none";
  }
}

constexpr const char *kAppTitle = "Quotes Clock";
constexpr const char *kBuildText = "native-idf";

struct TimezoneOption {
  const char *label;
  const char *standard;
  const char *dst;
  const char *daylight;
};

constexpr TimezoneOption TIMEZONES[] = {
    {"UTC", "UTC0", "UTC0", "UTC0"},
    {"Europe/London", "GMT0", "GMT0BST-1,M3.5.0/1,M10.5.0/2", "BST-1"},
    {"Europe/Central", "CET-1", "CET-1CEST-2,M3.5.0/2,M10.5.0/3", "CEST-2"},
    {"Europe/Eastern", "EET-2", "EET-2EEST-3,M3.5.0/3,M10.5.0/4", "EEST-3"},
    {"US/Eastern", "EST5", "EST5EDT4,M3.2.0/2,M11.1.0/2", "EDT4"},
    {"US/Central", "CST6", "CST6CDT5,M3.2.0/2,M11.1.0/2", "CDT5"},
    {"US/Mountain", "MST7", "MST7MDT6,M3.2.0/2,M11.1.0/2", "MDT6"},
    {"US/Pacific", "PST8", "PST8PDT7,M3.2.0/2,M11.1.0/2", "PDT7"},
    {"Australia/Sydney", "AEST-10", "AEST-10AEDT-11,M10.1.0/2,M4.1.0/3", "AEDT-11"},
    {"Asia/Tokyo", "JST-9", "JST-9", "JST-9"},
};
constexpr int kTimezoneCount = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

const char *const LOGO[] = {
    "..................................................",
    "...................BBBBBBBBBBBB...................",
    ".................BBBBBBBBBBBBBBBBB................",
    "...............BBBBBBBB....BBBBBBBB...............",
    "..............BBBBBB..........BBBBBB..............",
    ".............BBBBB......BB......BBBBB.............",
    "............BBB.........BB.........BBB............",
    "...........BBB..........BB..........BBB...........",
    "..........BBB........................BBB..........",
    ".........BBBB........................BBBB.........",
    "........BBBB..........................BBBB........",
    "........BBB............................BBB........",
    "........BBB......B...............BBB...BBB........",
    "........BB.......BBB............BBB.....BB........",
    ".......BBB.........BB.........BBB.......BBB.......",
    ".......BBB..........BB......BBBB........BBB.......",
    ".......BBB...........BB....BBB..........BBB.......",
    ".......BBB............BBBBBBB...........BBB.......",
    ".......BBB.BBB.........BBBB.........BBB.BBB.......",
    ".......BBB.BBB.........BBBB.........BBB.BBB.......",
    ".......BBB..............BB..............BBB.......",
    ".......BBB..............................BBB.......",
    ".......BBB..............................BBB.......",
    "..WW...BBB..............................BBB...WW..",
    "..WWWWW..B..............................B..WWWWW..",
    "..WWWWWWW................................WWWWWWW..",
    "..WWWWWWWWW.............BB.............WWWWWWWWW..",
    ".....WWWWWWW............BB...........WWWWWWWW.....",
    "BBBBBB....WWWW..........B...........WWWW....BBBBBB",
    "BBBBBBBBB...WW......................WW...BBBBBBBBB",
    "BBBBBBBBBBBB.WW.....RRR....RRR.....WW.BBBBBBBBBBB.",
    ".BBBBBBBBBBBB.WW...RRR....RRR.....WW.BBBBBBBBBBBB.",
    ".BBBBBBBBBBBBB....RRR.....RR........BBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBB...RRRRR..RRRRR.....BBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBBB..RRRRRR.RRRRRR...BBBBBBBBBBBBBBB.",
    ".BBBBBBBBBBBBBBBB.RRRRRR.RRRRRR..BBBBBBBBBBBBBBBB.",
    "..BBBBBBBBBBBBBBBBRRRRRR.RRRRRRRBBBBBBBBBBBBBBBB..",
    "..BBBBBBB..BBBBBBB.RRRRR..RRRRR.BBBBBBB..BBBBBBB..",
    "..BBBBBBBBBBB..BBBB.RRR....RRR.BBB...BBBBBBBBBBB..",
    "..BBBBBBBBBBBBBB..BB..........BB..BBBBBBBBBBBBBB..",
    "..BBBBBBBBBBBBBBBBBBBB......BBBBBBBBBBBBBBBBBBBB..",
    ".............BBBBBB..BB....BB..BBBBBB.............",
    ".................BBBBBBB..BBBBBBB.................",
    "...................BBBBB..BBBBB...................",
    "....................BBBBBBBBBB....................",
    ".....................BBBBBBBB.....................",
    "......................BBBBBB......................",
    ".......................BBBB.......................",
    "........................BB........................",
    "..................................................",
};

uint32_t decode_utf8(const char *text, size_t length, size_t &offset) {
  if (text == nullptr || offset >= length)
    return 0;
  const auto first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80)
    return first;
  if ((first & 0xE0) == 0xC0 && offset < length) {
    const auto second = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  if ((first & 0xF0) == 0xE0 && offset + 1 < length) {
    const auto second = static_cast<uint8_t>(text[offset++]);
    const auto third = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  if ((first & 0xF8) == 0xF0 && offset + 2 < length) {
    const auto second = static_cast<uint8_t>(text[offset++]);
    const auto third = static_cast<uint8_t>(text[offset++]);
    const auto fourth = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
  }
  return '?';
}

uint32_t decode_utf8_z(const char *text, size_t &offset) {
  const auto first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80)
    return first;
  if ((first & 0xE0) == 0xC0 && text[offset] != '\0') {
    const auto second = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  if ((first & 0xF0) == 0xE0) {
    if (text[offset] == '\0')
      return '?';
    const auto second = static_cast<uint8_t>(text[offset++]);
    if (text[offset] == '\0')
      return '?';
    const auto third = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  if ((first & 0xF8) == 0xF0) {
    if (text[offset] == '\0')
      return '?';
    const auto second = static_cast<uint8_t>(text[offset++]);
    if (text[offset] == '\0')
      return '?';
    const auto third = static_cast<uint8_t>(text[offset++]);
    if (text[offset] == '\0')
      return '?';
    const auto fourth = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
  }
  return '?';
}

void remove_last_utf8_char(std::string &text) {
  if (text.empty())
    return;
  text.pop_back();
  while (!text.empty() && (static_cast<uint8_t>(text.back()) & 0xC0) == 0x80)
    text.pop_back();
}

bool wrap_whitespace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool preferred_wrap_codepoint(uint32_t codepoint) {
  return codepoint == '-' || codepoint == '/' || codepoint == '\\' || codepoint == '_' || codepoint == '.' ||
         codepoint == ':' || codepoint == ';' || codepoint == ',' || codepoint == 0x2013 || codepoint == 0x2014;
}

size_t next_utf8_offset(const std::string &text, size_t offset) {
  if (offset >= text.size())
    return text.size();
  size_t next = offset;
  (void)decode_utf8(text.c_str(), text.size(), next);
  return next > offset ? std::min(next, text.size()) : std::min(offset + 1, text.size());
}

const assets::Glyph *find_glyph(const assets::Font &font, uint32_t codepoint) {
  for (size_t i = 0; i < font.glyph_count; i++) {
    if (font.glyphs[i].codepoint == codepoint)
      return &font.glyphs[i];
  }
  for (size_t i = 0; i < font.glyph_count; i++) {
    if (font.glyphs[i].codepoint == '?')
      return &font.glyphs[i];
  }
  return nullptr;
}
}  // namespace

NativeDisplay::NativeDisplay() {
  apply_timezone();
  portENTER_CRITICAL(&g_watchdog_rtc_lock);
  if (g_watchdog_rtc_magic == WATCHDOG_RTC_MAGIC) {
    _watchdog_recoveries.store(g_watchdog_rtc_recoveries);
    _watchdog_last_reason.store(static_cast<int>(g_watchdog_rtc_last_reason));
  } else {
    g_watchdog_rtc_magic = WATCHDOG_RTC_MAGIC;
    g_watchdog_rtc_recoveries = 0;
    g_watchdog_rtc_last_reason = static_cast<uint32_t>(WatchdogReason::None);
  }
  portEXIT_CRITICAL(&g_watchdog_rtc_lock);
}

NativeDisplay &NativeDisplay::instance() {
  static NativeDisplay display;
  return display;
}

esp_err_t NativeDisplay::start() {
  if (_tasks_started.exchange(true))
    return ESP_OK;

  ESP_LOGI(kTag, "starting display tasks with layout=%d cadence=%d", _layout_mode.load(),
           _refresh_cadence_minutes.load());
  if (xTaskCreatePinnedToCore(task_thunk, "qc-display", 8192, this, 4, &_task, 1) != pdPASS) {
    _tasks_started.store(false);
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCore(watchdog_task_thunk, "qc-display-watch", 3072, this, 2, &_watchdog_task, 0) != pdPASS) {
    vTaskDelete(_task);
    _task = nullptr;
    _tasks_started.store(false);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void NativeDisplay::request_refresh(bool fast) {
  if (!fast || !_refresh_pending.load())
    _fast_pending.store(fast);
  _refresh_pending.store(true);
}

DisplayOptions NativeDisplay::options() const {
  return {
      _display_enabled.load(),
      _layout_mode.load(),
      _refresh_cadence_minutes.load(),
      _timezone.load(),
      _dst_mode.load(),
      _clock_format.load(),
      _content_margin.load(),
      _clock_visible.load(),
      _quote_visible.load(),
      _quote_time_specific_enabled.load(),
      _quote_classics_enabled.load(),
      _highlight_time_enabled.load(),
      _highlight_time_color.load(),
      _highlight_time_text_color.load(),
      _main_pane_bg_color.load(),
      _main_pane_text_color.load(),
      _sidebar_visible.load(),
      _sidebar_color.load(),
      _bottom_bar_visible.load(),
      _bottom_bar_bg_color.load(),
      _bottom_bar_text_color.load(),
      _top_bar_visible.load(),
      _top_bar_bg_color.load(),
      _top_bar_text_color.load(),
      _top_bar_date_format.load(),
      _watch_style.load(),
  };
}

void NativeDisplay::set_options(const DisplayOptions &options) {
  bool changed = false;
  const bool display_enabled = options.display_enabled;
  const int layout = std::clamp(options.layout, 0, 3);
  const int cadence = std::clamp(options.refresh_cadence_minutes, kQuoteCadenceMin, kQuoteCadenceMax);
  const int timezone = std::clamp(options.timezone, 0, kTimezoneCount - 1);
  const int dst_mode = std::clamp(options.dst_mode, kDstOff, kDstOn);
  const int clock_format = std::clamp(options.clock_format, kClock24Hour, kClock12Hour);
  const int content_margin = std::clamp(options.content_margin, 16, 72);
  const bool clock_visible = options.clock_visible;
  const bool quote_visible = options.quote_visible;
  bool quote_time_specific_enabled = options.quote_time_specific_enabled;
  const bool quote_classics_enabled = options.quote_classics_enabled;
  if (!quote_time_specific_enabled && !quote_classics_enabled)
    quote_time_specific_enabled = true;
  const bool highlight_time_enabled = options.highlight_time_enabled;
  const int highlight_time_color = std::clamp(options.highlight_time_color, kHighlightYellow, kHighlightWhite);
  const int highlight_time_text_color =
      std::clamp(options.highlight_time_text_color, kHighlightYellow, kHighlightWhite);
  const int main_pane_bg_color = std::clamp(options.main_pane_bg_color, kHighlightYellow, kHighlightWhite);
  const int main_pane_text_color = std::clamp(options.main_pane_text_color, kHighlightYellow, kHighlightWhite);
  const bool sidebar_visible = options.sidebar_visible;
  const int sidebar_color = std::clamp(options.sidebar_color, kHighlightYellow, kHighlightWhite);
  const bool bottom_bar_visible = options.bottom_bar_visible;
  const int bottom_bar_bg_color = std::clamp(options.bottom_bar_bg_color, kHighlightYellow, kHighlightWhite);
  const int bottom_bar_text_color = std::clamp(options.bottom_bar_text_color, kHighlightYellow, kHighlightWhite);
  const bool top_bar_visible = options.top_bar_visible;
  const int top_bar_bg_color = std::clamp(options.top_bar_bg_color, kHighlightYellow, kHighlightWhite);
  const int top_bar_text_color = std::clamp(options.top_bar_text_color, kHighlightYellow, kHighlightWhite);
  const int top_bar_date_format = std::clamp(options.top_bar_date_format, kDateFormatAuto, kDateFormatMax);
  const bool watch_style = options.watch_style;

  const bool display_enabled_changed = _display_enabled.exchange(display_enabled) != display_enabled;
  const bool layout_changed = _layout_mode.exchange(layout) != layout;
  const bool cadence_changed = _refresh_cadence_minutes.exchange(cadence) != cadence;
  const bool timezone_value_changed = _timezone.exchange(timezone) != timezone;
  const bool dst_changed = _dst_mode.exchange(dst_mode) != dst_mode;
  const bool clock_format_changed = _clock_format.exchange(clock_format) != clock_format;
  const bool content_margin_changed = _content_margin.exchange(content_margin) != content_margin;
  const bool clock_visible_changed = _clock_visible.exchange(clock_visible) != clock_visible;
  const bool quote_visible_changed = _quote_visible.exchange(quote_visible) != quote_visible;
  const bool quote_time_specific_changed =
      _quote_time_specific_enabled.exchange(quote_time_specific_enabled) != quote_time_specific_enabled;
  const bool quote_classics_changed = _quote_classics_enabled.exchange(quote_classics_enabled) != quote_classics_enabled;
  const bool highlight_enabled_changed = _highlight_time_enabled.exchange(highlight_time_enabled) != highlight_time_enabled;
  const bool highlight_color_changed = _highlight_time_color.exchange(highlight_time_color) != highlight_time_color;
  const bool highlight_text_color_changed =
      _highlight_time_text_color.exchange(highlight_time_text_color) != highlight_time_text_color;
  const bool main_bg_changed = _main_pane_bg_color.exchange(main_pane_bg_color) != main_pane_bg_color;
  const bool main_text_changed = _main_pane_text_color.exchange(main_pane_text_color) != main_pane_text_color;
  const bool sidebar_visible_changed = _sidebar_visible.exchange(sidebar_visible) != sidebar_visible;
  const bool sidebar_color_changed = _sidebar_color.exchange(sidebar_color) != sidebar_color;
  const bool bottom_bar_visible_changed = _bottom_bar_visible.exchange(bottom_bar_visible) != bottom_bar_visible;
  const bool bottom_bar_bg_changed = _bottom_bar_bg_color.exchange(bottom_bar_bg_color) != bottom_bar_bg_color;
  const bool bottom_bar_text_changed = _bottom_bar_text_color.exchange(bottom_bar_text_color) != bottom_bar_text_color;
  const bool top_bar_visible_changed = _top_bar_visible.exchange(top_bar_visible) != top_bar_visible;
  const bool top_bar_bg_changed = _top_bar_bg_color.exchange(top_bar_bg_color) != top_bar_bg_color;
  const bool top_bar_text_changed = _top_bar_text_color.exchange(top_bar_text_color) != top_bar_text_color;
  const bool top_bar_date_changed = _top_bar_date_format.exchange(top_bar_date_format) != top_bar_date_format;
  const bool watch_style_changed = _watch_style.exchange(watch_style) != watch_style;
  const bool timezone_changed = timezone_value_changed || dst_changed;

  changed = display_enabled_changed || layout_changed || cadence_changed || timezone_changed || clock_format_changed ||
            content_margin_changed || clock_visible_changed || quote_visible_changed || highlight_enabled_changed ||
            quote_time_specific_changed || quote_classics_changed || highlight_color_changed ||
            highlight_text_color_changed || main_bg_changed || main_text_changed || sidebar_visible_changed || sidebar_color_changed ||
            bottom_bar_visible_changed || bottom_bar_bg_changed || bottom_bar_text_changed || top_bar_visible_changed ||
            top_bar_bg_changed || top_bar_text_changed || top_bar_date_changed || watch_style_changed;

  if (timezone_changed)
    apply_timezone();
  ESP_LOGI(kTag,
           "display options applied: enabled=%d layout=%d cadence=%d margin=%d clock_visible=%d quote_visible=%d "
           "quote_time=%d quote_classics=%d highlight=%d bg=%d fg=%d main=%d/%d sidebar=%d/%d bottom=%d/%d/%d "
           "top=%d/%d/%d date=%d watch=%d changed=%d",
           display_enabled ? 1 : 0, layout, cadence, content_margin, clock_visible ? 1 : 0,
           quote_visible ? 1 : 0, quote_time_specific_enabled ? 1 : 0, quote_classics_enabled ? 1 : 0,
           highlight_time_enabled ? 1 : 0, highlight_time_color, highlight_time_text_color, main_pane_bg_color,
           main_pane_text_color, sidebar_visible ? 1 : 0, sidebar_color,
           bottom_bar_visible ? 1 : 0, bottom_bar_bg_color, bottom_bar_text_color, top_bar_visible ? 1 : 0,
           top_bar_bg_color, top_bar_text_color, top_bar_date_format, watch_style ? 1 : 0, changed ? 1 : 0);
  if (changed && _display_enabled.load())
    request_refresh(false);
}

DisplayStatus NativeDisplay::status() const {
  auto &quotes = quote_partition_store();
  return {
      _initialized.load(),
      _busy.load(),
      _refresh_count.load(),
      _last_refresh_ms.load(),
      _last_render_ms.load(),
      _last_init_ms.load(),
      _last_transfer_ms.load(),
      _last_panel_refresh_ms.load(),
      _last_sleep_ms.load(),
      static_cast<esp_err_t>(_last_error.load()),
      _last_refresh_partial.load(),
      _last_partial_bytes.load(),
      _last_partial_x_start.load(),
      _last_partial_x_end.load(),
      _last_partial_y_start.load(),
      _last_partial_y_end.load(),
      quotes.ready(),
      quotes.quote_count(),
      quotes.time_specific_count(),
      quotes.classic_count(),
      quotes.total_size(),
  };
}

void NativeDisplay::task_thunk(void *arg) {
  static_cast<NativeDisplay *>(arg)->task();
}

void NativeDisplay::watchdog_task_thunk(void *arg) {
  static_cast<NativeDisplay *>(arg)->watchdog_task();
}

void NativeDisplay::task() {
  _task_heartbeat_ms.store(millis());
  _last_error.store(setup());
  _initialized.store(_last_error.load() == ESP_OK);
  request_refresh(false);
  _task_heartbeat_ms.store(millis());

  while (true) {
    _task_heartbeat_ms.store(millis());
    if (_initialized.load() && !_busy.load() && should_schedule_refresh())
      request_refresh(_last_rendered_minute_key.load() >= 0);

    if (_initialized.load() && _refresh_pending.exchange(false)) {
      if (!_display_enabled.load()) {
        sleep_ms(250);
        continue;
      }

      _busy.store(true);
      _refresh_started_ms.store(millis());
      const int64_t started = esp_timer_get_time();
      int64_t stage_started = started;
      auto finish_stage = [&stage_started]() {
        const int64_t now = esp_timer_get_time();
        const auto elapsed = static_cast<uint32_t>((now - stage_started) / 1000);
        stage_started = now;
        return elapsed;
      };
      _last_render_ms.store(0);
      _last_init_ms.store(0);
      _last_transfer_ms.store(0);
      _last_panel_refresh_ms.store(0);
      _last_sleep_ms.store(0);
      const bool fast = _fast_pending.exchange(false);
      _render_layout_mode = std::clamp(_layout_mode.load(), 0, 3);
      const auto platform = platform_snapshot();
      const auto time = current_time();
      const int next_frame_kind = frame_kind();
      const int next_wifi_state = platform.wifi_connected ? 1 : 0;
      const std::string next_station_ssid = platform.station_ssid;
      const bool frame_kind_changed = _last_rendered_frame_kind.load() != next_frame_kind;
      const bool wifi_state_changed = _last_rendered_wifi_state.load() != next_wifi_state;
      const bool station_ssid_changed = next_frame_kind == 0 && _last_rendered_station_ssid != next_station_ssid;
      const bool pane_due = pane_refresh_due(time);
      const bool partial_allowed = !frame_kind_changed && !wifi_state_changed && !station_ssid_changed &&
                                   can_use_partial_refresh(fast);
      const bool clock_only_partial =
          partial_allowed && time.valid && _clock_visible.load() && !watch_style_active() && !pane_due &&
          _last_pane_refresh_minute_key.load() >= 0;
      const bool watch_style_partial =
          partial_allowed && time.valid && watch_style_active() && _last_pane_refresh_minute_key.load() >= 0;
      const bool partial = partial_allowed;
      const LogicalRect partial_rect =
          watch_style_partial ? watch_dirty_rect_for(time) : (clock_only_partial ? clock_dirty_rect_for(time) : main_pane_rect());
      const PartialWindow partial_window = partial_window_for(partial_rect);
      const PartialWindow *window = partial ? &partial_window : nullptr;

      _last_stage.store(1);
      if (clock_only_partial)
        render_clock_tick(time, partial_rect);
      else
        render_clock_frame();
      _last_render_ms.store(finish_stage());
      _task_heartbeat_ms.store(millis());
      _last_stage.store(2);
      esp_err_t err = init_panel(partial);
      _last_init_ms.store(finish_stage());
      _task_heartbeat_ms.store(millis());
      if (err == ESP_OK) {
        _last_stage.store(3);
        err = transfer_frame(window);
        _last_transfer_ms.store(finish_stage());
        _task_heartbeat_ms.store(millis());
      }
      if (err == ESP_OK) {
        _last_stage.store(4);
        err = refresh_panel(window);
        _last_panel_refresh_ms.store(finish_stage());
        _task_heartbeat_ms.store(millis());
      }
      if (err == ESP_OK) {
        _last_stage.store(5);
        err = sleep_panel(partial);
        _last_sleep_ms.store(finish_stage());
        _task_heartbeat_ms.store(millis());
      }

      _last_error.store(err);
      _last_refresh_ms.store(static_cast<uint32_t>((esp_timer_get_time() - started) / 1000));
      _last_refresh_partial.store(partial);
      _last_partial_bytes.store(partial ? partial_window.transfer_bytes : 0);
      _last_partial_x_start.store(partial ? partial_window.x_start : -1);
      _last_partial_x_end.store(partial ? partial_window.x_end : -1);
      _last_partial_y_start.store(partial ? partial_window.y_start : -1);
      _last_partial_y_end.store(partial ? partial_window.y_end : -1);
      if (err == ESP_OK) {
        _last_stage.store(6);
        _refresh_count.fetch_add(1);
        if (time.valid) {
          _last_rendered_minute_key.store(time.minute_key);
          if ((!clock_only_partial && !watch_style_partial) || !partial)
            _last_pane_refresh_minute_key.store(time.minute_key);
          if (!partial)
            _last_full_refresh_day_key.store(time.day_key);
          _last_rendered_clock_text = _clock_visible.load() ? clock_text_for(time) : "";
        } else {
          _last_rendered_clock_text.clear();
        }
        _last_rendered_layout.store(_render_layout_mode);
        _last_rendered_frame_kind.store(next_frame_kind);
        _last_rendered_wifi_state.store(next_wifi_state);
        _last_rendered_station_ssid = next_station_ssid;
      } else {
        ESP_LOGE(kTag, "display refresh failed: %s", esp_err_to_name(err));
      }
      _last_refresh_finished_ms.store(millis());
      _refresh_started_ms.store(0);
      _busy.store(false);
    }
    sleep_ms(250);
  }
}

void NativeDisplay::watchdog_task() {
  while (true) {
    sleep_ms(kWatchdogIntervalMs);

    const uint32_t now = millis();
    const uint32_t heartbeat = _task_heartbeat_ms.load();
    if (heartbeat != 0 && now - heartbeat > kTaskStaleMs)
      watchdog_restart(WatchdogReason::TaskStale, now - heartbeat);

    const uint32_t refresh_started = _refresh_started_ms.load();
    if (_busy.load() && refresh_started != 0 && now - refresh_started > kRefreshStaleMs)
      watchdog_restart(WatchdogReason::RefreshStale, now - refresh_started);

    if (!_initialized.load() || !_display_enabled.load() || _busy.load() || _refresh_pending.load())
      continue;

    const uint32_t refresh_finished = _last_refresh_finished_ms.load();
    if (refresh_finished == 0)
      continue;

    if (!_quote_visible.load())
      continue;

    const auto time = current_time();
    const int last_pane = _last_pane_refresh_minute_key.load();
    const int cadence = std::clamp(_refresh_cadence_minutes.load(), kQuoteCadenceMin, kQuoteCadenceMax);
    const int elapsed_minutes = time.minute_key - last_pane;
    if (!time.valid || last_pane < 0 || (elapsed_minutes >= 0 && elapsed_minutes < cadence))
      continue;

    const uint32_t overdue_ms = static_cast<uint32_t>(cadence * 60000) + kOverdueRefreshSlackMs;
    if (now - refresh_finished > overdue_ms) {
      _watchdog_last_reason.store(static_cast<int>(WatchdogReason::RefreshOverdue));
      _watchdog_recoveries.fetch_add(1);
      ESP_LOGW(kTag, "display watchdog requesting overdue refresh: age=%lu ms last=%d current=%d cadence=%d",
               static_cast<unsigned long>(now - refresh_finished), last_pane, time.minute_key, cadence);
      request_refresh(true);
    }
  }
}

void NativeDisplay::watchdog_restart(WatchdogReason reason, uint32_t age_ms) {
  const auto reason_value = static_cast<uint32_t>(reason);
  uint32_t recoveries = 0;
  portENTER_CRITICAL(&g_watchdog_rtc_lock);
  g_watchdog_rtc_magic = WATCHDOG_RTC_MAGIC;
  g_watchdog_rtc_last_reason = reason_value;
  g_watchdog_rtc_recoveries++;
  recoveries = g_watchdog_rtc_recoveries;
  portEXIT_CRITICAL(&g_watchdog_rtc_lock);
  _watchdog_last_reason.store(static_cast<int>(reason_value));
  _watchdog_recoveries.store(recoveries);
  ESP_LOGE(kTag, "display watchdog restarting app: reason=%s age=%lu ms stage=%s busy=%d busyPin=%d",
           watchdog_reason_name(static_cast<int>(reason_value)), static_cast<unsigned long>(age_ms),
           stage_name(_last_stage.load()), _busy.load() ? 1 : 0, gpio_get_level(kPinBusy));
  esp_restart();
}

bool NativeDisplay::should_schedule_refresh() {
  if (!_display_enabled.load())
    return false;
  const auto time = current_time();
  if (!time.valid && millis() < BOOT_STATUS_SCREEN_DELAY_MS)
    return false;
  if (!time.valid)
    return frame_kind() != _last_rendered_frame_kind.load();
  if (time.minute_key == _last_rendered_minute_key.load())
    return false;
  if (pane_refresh_due(time))
    return true;
  return _clock_visible.load();
}

bool NativeDisplay::pane_refresh_due(const TimeState &time) const {
  if (!time.valid || !_quote_visible.load())
    return false;
  const int cadence = std::clamp(_refresh_cadence_minutes.load(), kQuoteCadenceMin, kQuoteCadenceMax);
  const int last_pane = _last_pane_refresh_minute_key.load();
  if (last_pane < 0)
    return true;
  const int elapsed_minutes = time.minute_key - last_pane;
  return elapsed_minutes < 0 || elapsed_minutes >= cadence;
}

bool NativeDisplay::can_use_partial_refresh(bool requested) const {
  if (!requested)
    return false;
  if (_last_rendered_minute_key.load() < 0)
    return false;
  if (_last_rendered_layout.load() != _render_layout_mode)
    return false;
  const auto time = current_time();
  if (!time.valid)
    return false;
  if (_last_full_refresh_day_key.load() != time.day_key)
    return false;
  if (!_quote_visible.load() && _clock_visible.load() && _clock_format.load() == kClock12Hour) {
    const int last_minute_key = _last_rendered_minute_key.load();
    if (last_minute_key >= 0 && last_minute_key / 720 != time.minute_key / 720)
      return false;
  }
  return true;
}

int NativeDisplay::frame_kind() const {
  const auto time = current_time();
  int kind = time.valid ? 16 : (show_setup_instructions() ? 8 : 0);
  if (_top_bar_visible.load())
    kind |= 1;
  if (_bottom_bar_visible.load())
    kind |= 2;
  if (time.valid && _quote_visible.load())
    kind |= 4;
  if (time.valid && watch_style_active())
    kind |= 32;
  return kind;
}

void NativeDisplay::apply_timezone() const {
  setenv("TZ", timezone_spec(), 1);
  tzset();
}

const char *NativeDisplay::timezone_spec() const {
  const int index = std::clamp<int>(_timezone.load(), 0, kTimezoneCount - 1);
  switch (_dst_mode.load()) {
    case kDstOn:
      return TIMEZONES[index].daylight;
    case kDstAuto:
      return TIMEZONES[index].dst;
    default:
      return TIMEZONES[index].standard;
  }
}

esp_err_t NativeDisplay::setup() {
  QC_RETURN_ON_ERROR(setup_gpio());
  QC_RETURN_ON_ERROR(setup_spi());
  return ESP_OK;
}

esp_err_t NativeDisplay::setup_gpio() {
  QC_RETURN_ON_ERROR(gpio_reset_pin(kPinDc));
  QC_RETURN_ON_ERROR(gpio_set_direction(kPinDc, GPIO_MODE_OUTPUT));
  QC_RETURN_ON_ERROR(gpio_reset_pin(kPinReset));
  QC_RETURN_ON_ERROR(gpio_set_direction(kPinReset, GPIO_MODE_OUTPUT));
  QC_RETURN_ON_ERROR(gpio_reset_pin(kPinCs));
  QC_RETURN_ON_ERROR(gpio_set_direction(kPinCs, GPIO_MODE_OUTPUT));
  QC_RETURN_ON_ERROR(gpio_set_level(kPinCs, 1));
  if (kUseBitBangSpi) {
    QC_RETURN_ON_ERROR(gpio_reset_pin(kPinSck));
    QC_RETURN_ON_ERROR(gpio_set_direction(kPinSck, GPIO_MODE_OUTPUT));
    QC_RETURN_ON_ERROR(gpio_set_level(kPinSck, 0));
    QC_RETURN_ON_ERROR(gpio_reset_pin(kPinMosi));
    QC_RETURN_ON_ERROR(gpio_set_direction(kPinMosi, GPIO_MODE_OUTPUT));
    QC_RETURN_ON_ERROR(gpio_set_level(kPinMosi, 0));
  }
  QC_RETURN_ON_ERROR(gpio_reset_pin(kPinBusy));
  QC_RETURN_ON_ERROR(gpio_set_direction(kPinBusy, GPIO_MODE_INPUT));
  QC_RETURN_ON_ERROR(gpio_set_pull_mode(kPinBusy, GPIO_PULLUP_ONLY));
  return ESP_OK;
}

esp_err_t NativeDisplay::setup_spi() {
  if (kUseBitBangSpi)
    return ESP_OK;

  spi_bus_config_t bus{};
  bus.mosi_io_num = kPinMosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = kPinSck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kTransferChunkBytes;
  QC_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

  spi_device_interface_config_t dev{};
  dev.clock_speed_hz = kSpiClockHz;
  dev.mode = 0;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  QC_RETURN_ON_ERROR(spi_bus_add_device(SPI3_HOST, &dev, &_spi));
  return ESP_OK;
}

esp_err_t NativeDisplay::hard_reset() {
  sleep_ms(100);
  gpio_set_level(kPinReset, 0);
  sleep_ms(10);
  gpio_set_level(kPinReset, 1);
  sleep_ms(10);
  return wait_idle(5000);
}

esp_err_t NativeDisplay::init_panel(bool partial) {
  if (partial && _controller_standby.load()) {
    const uint8_t active[] = {PSR_ACTIVE, PSR_FLAGS};
    QC_RETURN_ON_ERROR(cmd_data(CMD_PANEL_SETTING, active, sizeof(active)));
    QC_RETURN_ON_ERROR(command(CMD_POWERON));
    sleep_ms(10);
    return wait_idle(10000);
  }

  _controller_standby.store(false);
  QC_RETURN_ON_ERROR(hard_reset());
  for (const auto &entry : INIT_SEQUENCE) {
    QC_RETURN_ON_ERROR(cmd_data(entry[0], &entry[2], entry[1]));
  }

  QC_RETURN_ON_ERROR(command(CMD_POWERON));
  sleep_ms(10);
  QC_RETURN_ON_ERROR(wait_idle(10000));

  if (partial) {
    QC_RETURN_ON_ERROR(cmd_data(0xE0, 0x02));
    QC_RETURN_ON_ERROR(cmd_data(0xE6, 0x5A));
    QC_RETURN_ON_ERROR(cmd_data(0xA5, 0x00));
    QC_RETURN_ON_ERROR(wait_idle(5000));
  }

  return ESP_OK;
}

esp_err_t NativeDisplay::transfer_frame(const PartialWindow *window) {
  if (window != nullptr) {
    QC_RETURN_ON_ERROR(write_partial_window(*window));
    QC_RETURN_ON_ERROR(command(CMD_TRANSFER));
    return transfer_partial_window(*window);
  }

  QC_RETURN_ON_ERROR(command(CMD_TRANSFER));
  return data(_buffer.data(), _buffer.size());
}

esp_err_t NativeDisplay::transfer_partial_window(const PartialWindow &window) {
  std::array<uint8_t, kTransferChunkBytes> chunk{};
  size_t chunk_length = 0;

  for (uint16_t row = 0; row < window.rows; row++) {
    const uint32_t source = static_cast<uint32_t>(window.y_start + row) * kRowBytes + window.byte_start;
    for (uint16_t column = 0; column < window.row_bytes; column++) {
      chunk[chunk_length++] = _buffer[source + column];
      if (chunk_length == chunk.size()) {
        QC_RETURN_ON_ERROR(data(chunk.data(), chunk_length));
        chunk_length = 0;
      }
    }
  }

  if (chunk_length > 0)
    QC_RETURN_ON_ERROR(data(chunk.data(), chunk_length));
  return ESP_OK;
}

esp_err_t NativeDisplay::refresh_panel(const PartialWindow *window) {
  if (window != nullptr)
    QC_RETURN_ON_ERROR(write_partial_window(*window));
  QC_RETURN_ON_ERROR(cmd_data(CMD_REFRESH, 0x00));
  return wait_busy_then_idle(1000, 45000);
}

esp_err_t NativeDisplay::sleep_panel(bool partial) {
  if (partial) {
    const uint8_t standby[] = {PSR_STANDBY, PSR_FLAGS};
    QC_RETURN_ON_ERROR(cmd_data(CMD_PANEL_SETTING, standby, sizeof(standby)));
    _controller_standby.store(true);
    return ESP_OK;
  }

  QC_RETURN_ON_ERROR(cmd_data(CMD_POWEROFF, 0x00));
  sleep_ms(10);
  QC_RETURN_ON_ERROR(wait_idle(10000));
  _controller_standby.store(false);
  return cmd_data(CMD_DEEPSLEEP, 0xA5);
}

esp_err_t NativeDisplay::write_partial_window(const PartialWindow &window) {
  const uint8_t values[] = {
      static_cast<uint8_t>((window.x_start >> 8) & 0x03),
      static_cast<uint8_t>(window.x_start & 0xFC),
      static_cast<uint8_t>((window.x_end >> 8) & 0x03),
      static_cast<uint8_t>(window.x_end & 0xFC),
      static_cast<uint8_t>((window.y_start >> 8) & 0x03),
      static_cast<uint8_t>(window.y_start & 0xFF),
      static_cast<uint8_t>((window.y_end >> 8) & 0x03),
      static_cast<uint8_t>(window.y_end & 0xFF),
      0x01,
  };
  ESP_LOGD(kTag, "partial window x=%u..%u y=%u..%u bytes=%lu", window.x_start, window.x_end, window.y_start,
           window.y_end, static_cast<unsigned long>(window.transfer_bytes));
  return cmd_data(CMD_PARTIAL_WINDOW, values, sizeof(values));
}

esp_err_t NativeDisplay::transmit(const uint8_t *values, size_t length) {
  if (kUseBitBangSpi) {
    for (size_t i = 0; i < length; i++) {
      const uint8_t value = values[i];
      for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(kPinSck, 0);
        gpio_set_level(kPinMosi, (value >> bit) & 0x01);
        esp_rom_delay_us(1);
        gpio_set_level(kPinSck, 1);
        esp_rom_delay_us(1);
      }
    }
    gpio_set_level(kPinSck, 0);
    return ESP_OK;
  }

  spi_transaction_t tx{};
  tx.length = length * 8;
  tx.tx_buffer = values;
  return spi_device_polling_transmit(_spi, &tx);
}

esp_err_t NativeDisplay::command(uint8_t value) {
  gpio_set_level(kPinDc, 0);
  gpio_set_level(kPinCs, 0);
  const esp_err_t err = transmit(&value, 1);
  gpio_set_level(kPinCs, 1);
  return err;
}

esp_err_t NativeDisplay::data(const uint8_t *values, size_t length) {
  gpio_set_level(kPinDc, 1);
  while (length > 0) {
    const size_t chunk = std::min<size_t>(length, kTransferChunkBytes);
    gpio_set_level(kPinCs, 0);
    const esp_err_t err = transmit(values, chunk);
    gpio_set_level(kPinCs, 1);
    QC_RETURN_ON_ERROR(err);
    values += chunk;
    length -= chunk;
  }
  return ESP_OK;
}

esp_err_t NativeDisplay::data(uint8_t value) {
  return data(&value, 1);
}

esp_err_t NativeDisplay::cmd_data(uint8_t cmd, const uint8_t *values, size_t length) {
  if (kUseBitBangSpi) {
    QC_RETURN_ON_ERROR(command(cmd));
    return data(values, length);
  }

  gpio_set_level(kPinDc, 0);
  gpio_set_level(kPinCs, 0);
  esp_err_t err = transmit(&cmd, 1);
  if (err == ESP_OK && length > 0) {
    gpio_set_level(kPinDc, 1);
    while (length > 0 && err == ESP_OK) {
      const size_t chunk = std::min<size_t>(length, kTransferChunkBytes);
      err = transmit(values, chunk);
      values += chunk;
      length -= chunk;
    }
  }
  gpio_set_level(kPinCs, 1);
  return err;
}

esp_err_t NativeDisplay::cmd_data(uint8_t cmd, uint8_t value) {
  return cmd_data(cmd, &value, 1);
}

esp_err_t NativeDisplay::wait_idle(uint32_t timeout_ms) {
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (esp_timer_get_time() < deadline) {
    if (gpio_get_level(kPinBusy) == 1)
      return ESP_OK;
    sleep_ms(10);
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t NativeDisplay::wait_busy(uint32_t timeout_ms) {
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (esp_timer_get_time() < deadline) {
    if (gpio_get_level(kPinBusy) == 0)
      return ESP_OK;
    sleep_ms(1);
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t NativeDisplay::wait_busy_then_idle(uint32_t busy_timeout_ms, uint32_t idle_timeout_ms) {
  QC_RETURN_ON_ERROR(wait_busy(busy_timeout_ms));
  return wait_idle(idle_timeout_ms);
}

void NativeDisplay::render_clock_frame() {
  using namespace quotes_clock::assets;

  fill(Color::White);
  const bool portrait = portrait_layout();
  const int width = logical_width();
  const int height = logical_height();
  const int header = header_height();
  const int footer = footer_height();
  const bool top_bar_visible = _top_bar_visible.load();
  const bool bottom_bar_visible = _bottom_bar_visible.load();
  const int main_bg_color = _main_pane_bg_color.load();
  const int main_text_color = _main_pane_text_color.load();

  if (top_bar_visible) {
    rect_display_color(0, 0, width, header, _top_bar_bg_color.load());
    rect(portrait ? 348 : 650, 0, portrait ? 132 : 150, header, Color::Yellow);
  }
  if (bottom_bar_visible)
    rect_display_color(0, height - footer, width, footer, _bottom_bar_bg_color.load());
  rect_display_color(0, header, width, height - header - footer, main_bg_color);

  const auto time = current_time();
  char date_text[48] = {};
  if (top_bar_visible) {
    if (time.valid) {
      const int format = std::clamp(_top_bar_date_format.load(), kDateFormatAuto, kDateFormatMax);
      const char *date_format = portrait ? "%a, %d %b %Y" : "%A, %d %B %Y";
      switch (format) {
        case 1:
          date_format = "%Y-%m-%d";
          break;
        case 2:
          date_format = "%a, %d %b %Y";
          break;
        case 3:
          date_format = "%A, %d %B %Y";
          break;
        case 4:
          date_format = "%a, %b %d, %Y";
          break;
        case 5:
          date_format = "%A, %B %d, %Y";
          break;
        default:
          break;
      }
      std::strftime(date_text, sizeof(date_text), date_format, &time.local);
      draw_text_display_color(portrait ? 18 : 28, portrait ? 14 : 12, kFontHeaderDate, _top_bar_text_color.load(),
                              date_text);
    } else {
      draw_text_display_color(portrait ? 18 : 28, portrait ? 14 : 12, kFontHeaderDate, _top_bar_text_color.load(),
                              "Waiting for time");
    }
  }

  if (bottom_bar_visible) {
    const int footer_text_color = _bottom_bar_text_color.load();
    draw_text_display_color(portrait ? 8 : 12, height - 15, kFontFooter, footer_text_color, kAppTitle);
    draw_text_display_color(width - (portrait ? 8 : 12), height - 15, kFontFooter, footer_text_color, kBuildText,
                            TextAlign::Right);
    draw_wifi_status_icon(width / 2, height - 8, footer_text_color);
  }
  if (top_bar_visible)
    draw_logo(portrait ? 389 : 700, portrait ? 3 : 1);

  const int content_margin = std::clamp(_content_margin.load(), 16, 72);
  if (!time.valid) {
    const auto platform = platform_snapshot();
    const int wait_x = portrait ? content_margin : content_margin + 16;
    const int wait_title_y = portrait ? 144 : 124;
    const int wait_max_width = width - wait_x - content_margin;
    const int wait_bottom = height - footer - content_margin;
    auto line_capacity = [&](int y, int line_height) {
      return std::max(1, (wait_bottom - y) / line_height);
    };
    auto draw_status_line = [&](int y, const assets::Font &font, int text_color, const char *text, int extra_gap) {
      const int line_height = font.line_height + 8;
      bool status_fitted = false;
      const auto lines = wrap_text(text, font, wait_max_width, line_capacity(y, line_height), status_fitted);
      for (size_t i = 0; i < lines.size(); i++)
        draw_text_display_color(wait_x, y + static_cast<int>(i) * line_height, font, text_color, lines[i].text.c_str());
      return y + static_cast<int>(lines.size()) * line_height + extra_gap;
    };
    if (show_setup_instructions()) {
      int y = draw_status_line(wait_title_y, kFontTitle, main_text_color, "Setup mode", 18);
      y = draw_status_line(y, kFontBody, main_text_color, "Connect to Wi-Fi:", 6);
      y = draw_status_line(y, kFontBody, main_text_color, platform.fallback_ap_ssid.c_str(), 12);
      y = draw_status_line(y, kFontBody, main_text_color, "Open network, no password.", 12);
      y = draw_status_line(y, kFontBody, main_text_color, "Then open https://192.168.4.1", 22);
      draw_status_line(y, kFontMeta, main_text_color, "Set admin password and Wi-Fi details.", 0);
    } else {
      int y = draw_status_line(wait_title_y, kFontTitle, main_text_color, "Connecting to Wi-Fi", 18);
      y = draw_status_line(y, kFontBody, main_text_color, "SSID:", 6);
      y = draw_status_line(y, kFontBody, main_text_color, platform.station_ssid.c_str(), 22);
      draw_status_line(y, kFontMeta, main_text_color, "Setup mode starts if Wi-Fi cannot connect.", 0);
    }
    return;
  }

  const bool clock_visible = _clock_visible.load();
  const bool quote_visible = _quote_visible.load();
  if (watch_style_active()) {
    draw_watch_style_frame(time);
    return;
  }
  if (clock_visible && !quote_visible) {
    draw_clock_only(time, content_margin, main_text_color);
    return;
  }
  if (clock_visible) {
    const int clock_x = portrait ? content_margin + 2 : content_margin + 34;
    const int clock_y = portrait ? 100 : 92;
    draw_clock_text(time, clock_x, clock_y, kFontClock, kFontClockSuffix, main_text_color);
  }

  if (!quote_visible)
    return;

  DecodedQuote quote;
  char fallback_text[128] = {};
  if (!quote_for_time(time, quote)) {
    std::snprintf(fallback_text, sizeof(fallback_text),
                  "At %02d:%02d, the clock is awake and waiting for a curated literary minute.",
                  time.local.tm_hour, time.local.tm_min);
    quote.text = fallback_text;
    quote.title = "Quotes Clock Fallback";
    quote.author = "Quotes Clock";
    quote.highlight_offset = 3;
    quote.highlight_length = 5;
    quote.time_specific = true;
  }

  struct QuoteLayout {
    const Font *font;
    int line_height;
    int max_lines;
  };
  QuoteLayout layouts[] = {
      {&kFontQuote34, 44, portrait ? 5 : 3},
      {&kFontQuote30, 39, portrait ? 6 : 4},
      {&kFontQuote26, 34, portrait ? 8 : 4},
      {&kFontQuote22, 30, portrait ? 10 : 5},
      {&kFontQuote18, 24, portrait ? 13 : 6},
  };

  const bool sidebar_visible = _sidebar_visible.load();
  const int quote_bar_x = content_margin;
  const int quote_x = sidebar_visible ? quote_bar_x + 24 : quote_bar_x;
  const int quote_right_x = std::max(quote_x + 160, width - content_margin);
  const int quote_max_width = quote_right_x - quote_x;
  const int quote_y = clock_visible ? (portrait ? 280 : 248) : (portrait ? 104 : 96);
  const int quote_bar_y = quote_y + 2;
  const int meta_right_x = quote_right_x;
  bool title_fitted = false;
  bool author_fitted = false;
  const int title_line_height = std::max<int>(24, kFontBookTitle.line_height);
  const int author_line_height = std::max<int>(24, kFontBookAuthor.line_height);
  auto title_lines = wrap_text(quote.title.c_str(), kFontBookTitle, quote_max_width, portrait ? 3 : 2, title_fitted);
  auto author_lines = wrap_text(quote.author.c_str(), kFontBookAuthor, quote_max_width, portrait ? 2 : 1, author_fitted);
  const int meta_gap = (!title_lines.empty() && !author_lines.empty()) ? 4 : 0;
  const int title_block_height = wrapped_block_height(title_lines, kFontBookTitle, title_line_height);
  const int author_block_height = wrapped_block_height(author_lines, kFontBookAuthor, author_line_height);
  const int meta_height = title_block_height + meta_gap + author_block_height;
  const int meta_bottom = height - footer - content_margin;
  const int meta_y = std::max(quote_y + 40, meta_bottom - meta_height);
  auto max_lines_for = [&](const QuoteLayout &layout) {
    if (clock_visible)
      return layout.max_lines;
    return std::max(1, std::min(16, (meta_y - quote_y + 12) / layout.line_height));
  };
  bool fitted = false;
  QuoteLayout selected = layouts[4];
  std::vector<WrappedLine> quote_lines;
  for (const auto &layout : layouts) {
    quote_lines = wrap_text(quote.text.c_str(), *layout.font, quote_max_width, max_lines_for(layout), fitted);
    if (fitted) {
      selected = layout;
      break;
    }
  }
  quote_lines = wrap_text(quote.text.c_str(), *selected.font, quote_max_width, max_lines_for(selected), fitted);

  const int quote_bar_height = std::max(28, meta_y - quote_bar_y - 18);
  if (sidebar_visible)
    rect_display_color(quote_bar_x, quote_bar_y, 12, quote_bar_height, _sidebar_color.load());
  for (size_t line = 0; line < quote_lines.size(); line++) {
    const int line_y = quote_y + static_cast<int>(line) * selected.line_height;
    draw_quote_line(quote_x, line_y, selected.line_height, *selected.font, quote_lines[line],
                    _highlight_time_enabled.load() && quote.time_specific, quote.highlight_offset, quote.highlight_length,
                    _highlight_time_color.load(), _highlight_time_text_color.load(), main_text_color);
  }
  int meta_line_y = meta_y;
  for (size_t i = 0; i < title_lines.size(); i++) {
    draw_text_display_color(meta_right_x, meta_y + static_cast<int>(i) * title_line_height, kFontBookTitle, main_text_color, title_lines[i].text.c_str(),
                            TextAlign::Right);
  }
  meta_line_y = meta_y + title_block_height + meta_gap;
  for (size_t i = 0; i < author_lines.size(); i++) {
    draw_text_display_color(meta_right_x, meta_line_y + static_cast<int>(i) * author_line_height, kFontBookAuthor, main_text_color, author_lines[i].text.c_str(),
                            TextAlign::Right);
  }
}

NativeDisplay::TimeState NativeDisplay::current_time() const {
  TimeState state{};
  const time_t now = ::time(nullptr);
  localtime_r(&now, &state.local);
  state.valid = state.local.tm_year >= (2024 - 1900);
  state.minute = state.local.tm_hour * 60 + state.local.tm_min;
  state.day_key = (state.local.tm_year * 366) + state.local.tm_yday;
  state.minute_key = state.day_key * 1440 + state.minute;
  return state;
}

bool NativeDisplay::quote_for_time(const TimeState &time, DecodedQuote &quote) const {
  auto &store = quote_partition_store();
  bool allow_time_specific = _quote_time_specific_enabled.load();
  const bool allow_classics = _quote_classics_enabled.load();
  if (!allow_time_specific && !allow_classics)
    allow_time_specific = true;

  const int cadence = std::clamp(_refresh_cadence_minutes.load(), kQuoteCadenceMin, kQuoteCadenceMax);
  const uint32_t pane_selector =
      time.minute_key >= 0 ? static_cast<uint32_t>(time.minute_key / cadence) : static_cast<uint32_t>(time.minute);
  const bool prefer_classic = allow_classics && (!allow_time_specific || (pane_selector % 2U) == 1U);

  if (!prefer_classic && allow_time_specific &&
      store.decode_minute(time.minute, quote.text, quote.title, quote.author, quote.highlight_offset,
                          quote.highlight_length)) {
    quote.time_specific = true;
    return true;
  }

  if (allow_classics &&
      store.decode_classic(pane_selector / (allow_time_specific ? 2U : 1U), quote.text, quote.title, quote.author,
                           quote.highlight_offset, quote.highlight_length)) {
    quote.time_specific = false;
    return true;
  }

  if (allow_time_specific &&
      store.decode_minute(time.minute, quote.text, quote.title, quote.author, quote.highlight_offset,
                          quote.highlight_length)) {
    quote.time_specific = true;
    return true;
  }

  return false;
}

int NativeDisplay::header_height() const {
  if (!_top_bar_visible.load())
    return 0;
  return portrait_layout() ? 56 : 52;
}

int NativeDisplay::footer_height() const {
  return _bottom_bar_visible.load() ? kFooterHeight : 0;
}

int NativeDisplay::logical_width() const {
  return portrait_layout() ? kHeight : kWidth;
}

int NativeDisplay::logical_height() const {
  return portrait_layout() ? kWidth : kHeight;
}

bool NativeDisplay::portrait_layout() const {
  const int layout = _render_layout_mode;
  return layout == 2 || layout == 3;
}

bool NativeDisplay::watch_style_active() const {
  return _watch_style.load() && _clock_visible.load() && !_quote_visible.load() && !_top_bar_visible.load() &&
         !_bottom_bar_visible.load();
}

int NativeDisplay::wifi_signal_bars() const {
  const auto platform = platform_snapshot();
  if (!platform.wifi_connected)
    return 0;
  if (platform.wifi_rssi >= -55)
    return 4;
  if (platform.wifi_rssi >= -67)
    return 3;
  if (platform.wifi_rssi >= -75)
    return 2;
  return 1;
}

bool NativeDisplay::show_setup_instructions() const {
  if (millis() < SETUP_SCREEN_GRACE_MS)
    return false;
  return platform_snapshot().fallback_ap_active;
}

void NativeDisplay::render_clock_tick(const TimeState &time, const LogicalRect &dirty_rect) {
  const int width = std::max(0, dirty_rect.x_end - dirty_rect.x_start + 1);
  const int height = std::max(0, dirty_rect.y_end - dirty_rect.y_start + 1);
  if (width <= 0 || height <= 0)
    return;

  rect_display_color(dirty_rect.x_start, dirty_rect.y_start, width, height, _main_pane_bg_color.load());

  const int content_margin = std::clamp(_content_margin.load(), 16, 72);
  const int text_color = _main_pane_text_color.load();
  if (!_quote_visible.load()) {
    draw_clock_only(time, content_margin, text_color);
  } else {
    const int clock_x = portrait_layout() ? content_margin + 2 : content_margin + 34;
    const int clock_y = portrait_layout() ? 100 : 92;
    draw_clock_text(time, clock_x, clock_y, quotes_clock::assets::kFontClock, quotes_clock::assets::kFontClockSuffix,
                    text_color);
  }
}

void NativeDisplay::draw_clock_text(const TimeState &time, int x, int y, const assets::Font &clock_font,
                                    const assets::Font &suffix_font, int text_color) {
  char clock_text[8] = {};
  if (_clock_format.load() == kClock12Hour) {
    const int hour12 = time.local.tm_hour % 12 == 0 ? 12 : time.local.tm_hour % 12;
    const char *suffix = time.local.tm_hour < 12 ? "AM" : "PM";
    std::snprintf(clock_text, sizeof(clock_text), "%d:%02d", hour12, time.local.tm_min);
    draw_text_display_color(x, y, clock_font, text_color, clock_text);
    const int suffix_x = x + text_width(clock_font, clock_text) + std::max(8, suffix_font.line_height / 4);
    const int suffix_y = y + clock_font.baseline - suffix_font.baseline;
    draw_text_display_color(suffix_x, suffix_y, suffix_font, text_color, suffix);
  } else {
    std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d", time.local.tm_hour, time.local.tm_min);
    draw_text_display_color(x, y, clock_font, text_color, clock_text);
  }
}

void NativeDisplay::draw_clock_only(const TimeState &time, int content_margin, int text_color) {
  using namespace quotes_clock::assets;
  struct ClockLayout {
    const Font *clock_font;
    const Font *suffix_font;
  };
  const ClockLayout layouts[] = {
      {&kFontClock300, &kFontClockSuffix150},
      {&kFontClock260, &kFontClockSuffix130},
      {&kFontClock220, &kFontClockSuffix110},
      {&kFontClock180, &kFontClockSuffix90},
      {&kFontClock150, &kFontClockSuffix75},
      {&kFontClock120, &kFontClockSuffix60},
      {&kFontClock, &kFontClockSuffix},
  };

  const int pane_y = header_height();
  const int pane_height = logical_height() - pane_y - footer_height();
  const int max_width = logical_width() - content_margin * 2;
  const int max_height = std::max(1, pane_height - content_margin * 2);
  char clock_text[8] = {};
  const bool twelve_hour = _clock_format.load() == kClock12Hour;
  if (twelve_hour) {
    const int hour12 = time.local.tm_hour % 12 == 0 ? 12 : time.local.tm_hour % 12;
    std::snprintf(clock_text, sizeof(clock_text), "%d:%02d", hour12, time.local.tm_min);
  } else {
    std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d", time.local.tm_hour, time.local.tm_min);
  }

  const ClockLayout *selected = &layouts[(sizeof(layouts) / sizeof(layouts[0])) - 1];
  for (const auto &layout : layouts) {
    const char *suffix = time.local.tm_hour < 12 ? "AM" : "PM";
    const int gap = twelve_hour ? std::max(8, layout.suffix_font->line_height / 4) : 0;
    const int suffix_width = twelve_hour ? text_width(*layout.suffix_font, suffix) : 0;
    const int total_width = text_width(*layout.clock_font, clock_text) + gap + suffix_width;
    const int total_height = std::max<int>(layout.clock_font->line_height, twelve_hour ? layout.suffix_font->line_height : 0);
    if (total_width <= max_width && total_height <= max_height) {
      selected = &layout;
      break;
    }
  }

  const char *suffix = time.local.tm_hour < 12 ? "AM" : "PM";
  const int gap = twelve_hour ? std::max(8, selected->suffix_font->line_height / 4) : 0;
  const int suffix_width = twelve_hour ? text_width(*selected->suffix_font, suffix) : 0;
  const int total_width = text_width(*selected->clock_font, clock_text) + gap + suffix_width;
  const int total_height = std::max<int>(selected->clock_font->line_height, twelve_hour ? selected->suffix_font->line_height : 0);
  const int x = content_margin + std::max(0, (max_width - total_width) / 2);
  const int y = pane_y + content_margin + std::max(0, (max_height - total_height) / 2);
  draw_clock_text(time, x, y, *selected->clock_font, *selected->suffix_font, text_color);
}

std::string NativeDisplay::clock_text_for(const TimeState &time) const {
  char clock_text[12] = {};
  if (_clock_format.load() == kClock12Hour) {
    const int hour12 = time.local.tm_hour % 12 == 0 ? 12 : time.local.tm_hour % 12;
    const char *suffix = time.local.tm_hour < 12 ? "AM" : "PM";
    std::snprintf(clock_text, sizeof(clock_text), "%d:%02d %s", hour12, time.local.tm_min, suffix);
  } else {
    std::snprintf(clock_text, sizeof(clock_text), "%02d:%02d", time.local.tm_hour, time.local.tm_min);
  }
  return clock_text;
}

NativeDisplay::LogicalRect NativeDisplay::clock_rect_for() const {
  using namespace quotes_clock::assets;
  const int content_margin = std::clamp(_content_margin.load(), 16, 72);
  const bool twelve_hour = _clock_format.load() == kClock12Hour;
  const char *measure_text = twelve_hour ? "12:59" : "00:00";
  const char *suffix = "PM";
  const int pad = 16;

  const Font *clock_font = &kFontClock;
  const Font *suffix_font = &kFontClockSuffix;
  int x = portrait_layout() ? content_margin + 2 : content_margin + 34;
  int y = portrait_layout() ? 100 : 92;

  if (!_quote_visible.load()) {
    struct ClockLayout {
      const Font *clock_font;
      const Font *suffix_font;
    };
    const ClockLayout layouts[] = {
        {&kFontClock300, &kFontClockSuffix150},
        {&kFontClock260, &kFontClockSuffix130},
        {&kFontClock220, &kFontClockSuffix110},
        {&kFontClock180, &kFontClockSuffix90},
        {&kFontClock150, &kFontClockSuffix75},
        {&kFontClock120, &kFontClockSuffix60},
        {&kFontClock, &kFontClockSuffix},
    };
    const int pane_y = header_height();
    const int pane_height = logical_height() - pane_y - footer_height();
    const int max_width = logical_width() - content_margin * 2;
    const int max_height = std::max(1, pane_height - content_margin * 2);
    const ClockLayout *selected = &layouts[(sizeof(layouts) / sizeof(layouts[0])) - 1];
    for (const auto &layout : layouts) {
      const int gap = twelve_hour ? std::max(8, layout.suffix_font->line_height / 4) : 0;
      const int suffix_width = twelve_hour ? text_width(*layout.suffix_font, suffix) : 0;
      const int total_width = text_width(*layout.clock_font, measure_text) + gap + suffix_width;
      const int total_height =
          std::max<int>(layout.clock_font->line_height, twelve_hour ? layout.suffix_font->line_height : 0);
      if (total_width <= max_width && total_height <= max_height) {
        selected = &layout;
        break;
      }
    }
    clock_font = selected->clock_font;
    suffix_font = selected->suffix_font;
    const int gap = twelve_hour ? std::max(8, suffix_font->line_height / 4) : 0;
    const int suffix_width = twelve_hour ? text_width(*suffix_font, suffix) : 0;
    const int total_width = text_width(*clock_font, measure_text) + gap + suffix_width;
    const int total_height = std::max<int>(clock_font->line_height, twelve_hour ? suffix_font->line_height : 0);
    x = content_margin + std::max(0, (max_width - total_width) / 2);
    y = pane_y + content_margin + std::max(0, (max_height - total_height) / 2);
  }

  const int gap = twelve_hour ? std::max(8, suffix_font->line_height / 4) : 0;
  const int suffix_width = twelve_hour ? text_width(*suffix_font, suffix) : 0;
  const int width = text_width(*clock_font, measure_text) + gap + suffix_width;
  const int height = std::max<int>(clock_font->line_height, twelve_hour ? suffix_font->line_height : 0);
  return {
      std::max(0, x - pad),
      std::max(header_height(), y - pad),
      std::min(logical_width() - 1, x + width + pad),
      std::min(logical_height() - footer_height() - 1, y + height + pad),
  };
}

NativeDisplay::LogicalRect NativeDisplay::clock_dirty_rect_for(const TimeState &time) const {
  using namespace quotes_clock::assets;
  const LogicalRect whole_clock = clock_rect_for();
  if (!time.valid || _clock_format.load() != kClock24Hour)
    return whole_clock;

  const std::string current = clock_text_for(time);
  const std::string previous = _last_rendered_clock_text;
  if (previous.empty() || previous.size() != current.size() || previous == current)
    return whole_clock;

  const int content_margin = std::clamp(_content_margin.load(), 16, 72);
  const Font *clock_font = &kFontClock;
  int x = portrait_layout() ? content_margin + 2 : content_margin + 34;
  int y = portrait_layout() ? 100 : 92;

  if (!_quote_visible.load()) {
    struct ClockLayout {
      const Font *clock_font;
      const Font *suffix_font;
    };
    const ClockLayout layouts[] = {
        {&kFontClock300, &kFontClockSuffix150},
        {&kFontClock260, &kFontClockSuffix130},
        {&kFontClock220, &kFontClockSuffix110},
        {&kFontClock180, &kFontClockSuffix90},
        {&kFontClock150, &kFontClockSuffix75},
        {&kFontClock120, &kFontClockSuffix60},
        {&kFontClock, &kFontClockSuffix},
    };
    const int pane_y = header_height();
    const int pane_height = logical_height() - pane_y - footer_height();
    const int max_width = logical_width() - content_margin * 2;
    const int max_height = std::max(1, pane_height - content_margin * 2);
    const ClockLayout *selected = &layouts[(sizeof(layouts) / sizeof(layouts[0])) - 1];
    for (const auto &layout : layouts) {
      const int total_width = text_width(*layout.clock_font, "00:00");
      const int total_height = layout.clock_font->line_height;
      if (total_width <= max_width && total_height <= max_height) {
        selected = &layout;
        break;
      }
    }
    clock_font = selected->clock_font;
    const int total_width = text_width(*clock_font, "00:00");
    const int total_height = clock_font->line_height;
    x = content_margin + std::max(0, (max_width - total_width) / 2);
    y = pane_y + content_margin + std::max(0, (max_height - total_height) / 2);
  }

  if (text_width(*clock_font, current.c_str()) != text_width(*clock_font, previous.c_str()))
    return whole_clock;

  const int x_pad = 0;
  const int y_pad = 4;
  bool have_dirty = false;
  int dirty_x_start = whole_clock.x_end;
  int dirty_y_start = whole_clock.y_end;
  int dirty_x_end = whole_clock.x_start;
  int dirty_y_end = whole_clock.y_start;
  int cursor = x;

  auto include_glyph = [&](char value) {
    const auto *glyph = find_glyph(*clock_font, static_cast<unsigned char>(value));
    if (glyph == nullptr)
      return;
    const int gx_start = cursor + glyph->x_offset;
    const int gy_start = y + glyph->y_offset;
    const int gx_end = gx_start + glyph->width - 1;
    const int gy_end = gy_start + glyph->height - 1;
    dirty_x_start = std::min(dirty_x_start, gx_start);
    dirty_y_start = std::min(dirty_y_start, gy_start);
    dirty_x_end = std::max(dirty_x_end, gx_end);
    dirty_y_end = std::max(dirty_y_end, gy_end);
    have_dirty = true;
  };

  for (size_t i = 0; i < current.size(); i++) {
    const auto *glyph = find_glyph(*clock_font, static_cast<unsigned char>(current[i]));
    if (current[i] != previous[i]) {
      include_glyph(previous[i]);
      include_glyph(current[i]);
    }
    if (glyph != nullptr)
      cursor += glyph->x_advance;
  }

  if (!have_dirty)
    return whole_clock;

  return {
      std::max(whole_clock.x_start, dirty_x_start - x_pad),
      std::max(whole_clock.y_start, dirty_y_start - y_pad),
      std::min(whole_clock.x_end, dirty_x_end + x_pad),
      std::min(whole_clock.y_end, dirty_y_end + y_pad),
  };
}

NativeDisplay::WatchLayout NativeDisplay::watch_layout() const {
  const int content_margin = std::clamp(_content_margin.load(), 16, 72);
  WatchLayout layout{};
  layout.pane_x = content_margin;
  layout.pane_y = content_margin;
  layout.pane_w = std::max(1, logical_width() - content_margin * 2);
  layout.pane_h = std::max(1, logical_height() - content_margin * 2);

  const int scale_by_width = std::max(1, layout.pane_w / kWatchTimeUnits);
  const int scale_by_height = std::max(1, (layout.pane_h * 3) / (5 * kWatchDigitHeightUnits));
  layout.scale = std::clamp(std::min(scale_by_width, scale_by_height), 3, 12);
  layout.digit_w = kWatchDigitUnits * layout.scale;
  layout.digit_h = kWatchDigitHeightUnits * layout.scale;
  layout.digit_gap = kWatchDigitGapUnits * layout.scale;
  layout.colon_gap = kWatchColonGapUnits * layout.scale;
  layout.colon_w = kWatchColonUnits * layout.scale;

  const int time_w = kWatchTimeUnits * layout.scale;
  const int spare_x = std::max(0, layout.pane_w - time_w);
  const int bottom_gap = std::min(layout.scale, std::max(0, layout.pane_h - layout.digit_h));
  layout.digit_x = layout.pane_x + spare_x / 2;
  layout.digit_y = layout.pane_y + layout.pane_h - layout.digit_h - bottom_gap;
  layout.day_scale = std::clamp((layout.scale + 3) / 4, 2, 5);
  layout.small_scale = layout.day_scale;
  layout.day_center_x = layout.pane_x + layout.pane_w / 2;
  layout.day_y = layout.pane_y + std::max(0, layout.scale / 2);
  layout.mode_x = layout.pane_x + layout.scale;
  layout.mode_y = layout.pane_y + std::max(8, layout.day_scale * 12);
  layout.date_right_x = layout.pane_x + layout.pane_w;
  layout.date_y = layout.day_y;
  return layout;
}

NativeDisplay::LogicalRect NativeDisplay::watch_dirty_rect_for(const TimeState &time) const {
  const LogicalRect whole_pane = main_pane_rect();
  if (!time.valid)
    return whole_pane;

  const WatchLayout layout = watch_layout();
  const std::string current = clock_text_for(time);
  const std::string previous = _last_rendered_clock_text;

  auto extract_digits = [](const std::string &text, char digits[4]) {
    std::fill(digits, digits + 4, ' ');
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon + 2 >= text.size() || colon == 0)
      return false;
    if (colon == 1) {
      digits[1] = text[0];
    } else {
      digits[0] = text[colon - 2];
      digits[1] = text[colon - 1];
    }
    digits[2] = text[colon + 1];
    digits[3] = text[colon + 2];
    return true;
  };

  char current_digits[4] = {};
  char previous_digits[4] = {};
  if (!extract_digits(current, current_digits) || !extract_digits(previous, previous_digits)) {
    return {
        std::max(whole_pane.x_start, layout.digit_x),
        std::max(whole_pane.y_start, layout.digit_y),
        std::min(whole_pane.x_end, layout.digit_x + kWatchTimeUnits * layout.scale - 1),
        std::min(whole_pane.y_end, layout.digit_y + layout.digit_h - 1),
    };
  }

  const int digit_x[] = {
      layout.digit_x,
      layout.digit_x + layout.digit_w + layout.digit_gap,
      layout.digit_x + layout.digit_w * 2 + layout.digit_gap + layout.colon_gap * 2 + layout.colon_w,
      layout.digit_x + layout.digit_w * 3 + layout.digit_gap * 2 + layout.colon_gap * 2 + layout.colon_w,
  };
  const int pad = std::max(2, layout.scale / 2);
  bool have_dirty = false;
  LogicalRect dirty{whole_pane.x_end, whole_pane.y_end, whole_pane.x_start, whole_pane.y_start};

  auto include_rect = [&](int x0, int y0, int x1, int y1) {
    dirty.x_start = std::min(dirty.x_start, x0);
    dirty.y_start = std::min(dirty.y_start, y0);
    dirty.x_end = std::max(dirty.x_end, x1);
    dirty.y_end = std::max(dirty.y_end, y1);
    have_dirty = true;
  };

  for (int i = 0; i < 4; i++) {
    if (current_digits[i] != previous_digits[i]) {
      include_rect(digit_x[i] - pad, layout.digit_y - pad, digit_x[i] + layout.digit_w + pad,
                   layout.digit_y + layout.digit_h + pad);
    }
  }

  if (_clock_format.load() == kClock12Hour &&
      (previous.size() < 2 || current.size() < 2 ||
       previous.substr(previous.size() - 2) != current.substr(current.size() - 2))) {
    include_rect(layout.mode_x - pad, layout.mode_y - pad, layout.mode_x + 56 + pad,
                 layout.mode_y + quotes_clock::assets::kFontHeaderDate.line_height + pad);
  }

  if (!have_dirty)
    return whole_pane;

  return {
      std::max(whole_pane.x_start, dirty.x_start),
      std::max(whole_pane.y_start, dirty.y_start),
      std::min(whole_pane.x_end, dirty.x_end),
      std::min(whole_pane.y_end, dirty.y_end),
  };
}

NativeDisplay::LogicalRect NativeDisplay::main_pane_rect() const {
  return {0, header_height(), logical_width() - 1, logical_height() - footer_height() - 1};
}

NativeDisplay::PartialWindow NativeDisplay::partial_window_for(const LogicalRect &rect) const {
  int x0 = rect.x_start;
  int y0 = rect.y_start;
  int x1 = rect.x_end;
  int y1 = rect.y_start;
  int x2 = rect.x_start;
  int y2 = rect.y_end;
  int x3 = rect.x_end;
  int y3 = rect.y_end;

  if (!logical_to_physical(x0, y0) || !logical_to_physical(x1, y1) || !logical_to_physical(x2, y2) ||
      !logical_to_physical(x3, y3)) {
    return {0, kWidth - 1, 0, kHeight - 1, 0, kRowBytes, kHeight, kBufferBytes};
  }

  uint16_t x_start = static_cast<uint16_t>(std::min(std::min(x0, x1), std::min(x2, x3)));
  uint16_t x_end = static_cast<uint16_t>(std::max(std::max(x0, x1), std::max(x2, x3)));
  uint16_t y_start = static_cast<uint16_t>(std::min(std::min(y0, y1), std::min(y2, y3)));
  uint16_t y_end = static_cast<uint16_t>(std::max(std::max(y0, y1), std::max(y2, y3)));

  x_start &= 0xFFFC;
  x_end = std::min<uint16_t>(kWidth - 1, x_end | 0x0003);

  const uint16_t row_bytes = static_cast<uint16_t>((x_end - x_start + 1) / kPixelsPerByte);
  const uint16_t rows = static_cast<uint16_t>(y_end - y_start + 1);
  return {
      x_start,
      x_end,
      y_start,
      y_end,
      static_cast<uint16_t>(x_start / kPixelsPerByte),
      row_bytes,
      rows,
      static_cast<uint32_t>(row_bytes) * rows,
  };
}

bool NativeDisplay::logical_to_physical(int &x, int &y) const {
  const int layout = _render_layout_mode;
  int physical_x = x;
  int physical_y = y;
  switch (layout) {
    case 1:
      physical_x = kWidth - 1 - x;
      physical_y = kHeight - 1 - y;
      break;
    case 2:
      physical_x = kWidth - 1 - y;
      physical_y = x;
      break;
    case 3:
      physical_x = y;
      physical_y = kHeight - 1 - x;
      break;
    default:
      break;
  }

  if (physical_x < 0 || physical_y < 0 || physical_x >= kWidth || physical_y >= kHeight)
    return false;

  x = physical_x;
  y = physical_y;
  return true;
}

void NativeDisplay::draw_logo(int origin_x, int origin_y) {
  for (int y = 0; y < 50; y++) {
    for (int x = 0; x < 50; x++) {
      const char pixel_value = LOGO[y][x];
      if (pixel_value == 'B')
        logical_pixel(origin_x + x, origin_y + y, Color::Black);
      else if (pixel_value == 'R')
        logical_pixel(origin_x + x, origin_y + y, Color::Red);
      else if (pixel_value == 'W')
        logical_pixel(origin_x + x, origin_y + y, Color::White);
    }
  }
}

void NativeDisplay::draw_wifi_status_icon(int center_x, int center_y, int display_color) {
  const int bars = wifi_signal_bars();

  rect_display_color(center_x - 1, center_y + 4, 3, 3, display_color);

  auto draw_band = [&](int y, int half_width) {
    rect_display_color(center_x - half_width, y, half_width * 2 + 1, 1, display_color);
    rect_display_color(center_x - half_width + 2, y + 1, half_width * 2 - 3, 1, display_color);
  };

  if (bars >= 1)
    draw_band(center_y + 1, 4);
  if (bars >= 2)
    draw_band(center_y - 2, 7);
  if (bars >= 3)
    draw_band(center_y - 5, 10);
  if (bars >= 4)
    draw_band(center_y - 8, 13);

  if (bars == 0) {
    rect_display_color(center_x - 13, center_y - 8, 3, 2, display_color);
    rect_display_color(center_x - 9, center_y - 5, 3, 2, display_color);
    rect_display_color(center_x - 5, center_y - 2, 3, 2, display_color);
    rect(center_x - 13, center_y + 4, 27, 2, Color::Red);
    rect(center_x - 5, center_y - 2, 3, 8, Color::Red);
  }
}

void NativeDisplay::draw_watch_style_frame(const TimeState &time) {
  using namespace quotes_clock::assets;

  const int text_color = _main_pane_text_color.load();
  const WatchLayout layout = watch_layout();

  const char *days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  const int day_index = std::clamp(time.local.tm_wday, 0, 6);
  draw_watch_day_label(layout.day_center_x, layout.day_y, layout.day_scale, days[day_index], text_color);
  if (_clock_format.load() == kClock12Hour) {
    draw_text_display_color(layout.mode_x, layout.mode_y, kFontHeaderDate, text_color,
                            time.local.tm_hour < 12 ? "AM" : "PM");
  } else {
    draw_text_display_color(layout.mode_x, layout.mode_y, kFontHeaderDate, text_color, "24H");
  }

  const int small_digit_w = kWatchDigitUnits * layout.small_scale;
  const int small_gap = std::max(2, layout.small_scale);
  const int date_day = std::clamp(time.local.tm_mday, 1, 31);
  int date_x = layout.date_right_x - small_digit_w;
  if (date_day >= 10) {
    date_x -= small_digit_w + small_gap;
    draw_watch_digit(date_x, layout.date_y, layout.small_scale, date_day / 10, text_color);
    date_x += small_digit_w + small_gap;
  }
  draw_watch_digit(date_x, layout.date_y, layout.small_scale, date_day % 10, text_color);

  int hour = time.local.tm_hour;
  bool blank_first = false;
  if (_clock_format.load() == kClock12Hour) {
    hour = hour % 12 == 0 ? 12 : hour % 12;
    blank_first = hour < 10;
  }

  const int h0 = hour / 10;
  const int h1 = hour % 10;
  const int m0 = time.local.tm_min / 10;
  const int m1 = time.local.tm_min % 10;
  int x = layout.digit_x;
  draw_watch_digit(x, layout.digit_y, layout.scale, blank_first ? -1 : h0, text_color);
  x += layout.digit_w + layout.digit_gap;
  draw_watch_digit(x, layout.digit_y, layout.scale, h1, text_color);
  x += layout.digit_w + layout.colon_gap;
  draw_watch_colon(x, layout.digit_y, layout.scale, text_color);
  x += layout.colon_w + layout.colon_gap;
  draw_watch_digit(x, layout.digit_y, layout.scale, m0, text_color);
  x += layout.digit_w + layout.digit_gap;
  draw_watch_digit(x, layout.digit_y, layout.scale, m1, text_color);
}

void NativeDisplay::draw_watch_digit(int x, int y, int scale, int digit, int display_color) {
  if (digit < 0 || digit > 9)
    return;
  draw_watch_symbol(x, y, scale, static_cast<char>('0' + digit), display_color);
}

void NativeDisplay::draw_watch_symbol(int x, int y, int scale, char symbol, int display_color) {
  constexpr uint8_t kSegA = 1U << 0;
  constexpr uint8_t kSegB = 1U << 1;
  constexpr uint8_t kSegC = 1U << 2;
  constexpr uint8_t kSegD = 1U << 3;
  constexpr uint8_t kSegE = 1U << 4;
  constexpr uint8_t kSegF = 1U << 5;
  constexpr uint8_t kSegG = 1U << 6;
  constexpr uint8_t kDigits[] = {
      kSegA | kSegB | kSegC | kSegD | kSegE | kSegF,
      kSegB | kSegC,
      kSegA | kSegB | kSegD | kSegE | kSegG,
      kSegA | kSegB | kSegC | kSegD | kSegG,
      kSegB | kSegC | kSegF | kSegG,
      kSegA | kSegC | kSegD | kSegF | kSegG,
      kSegA | kSegC | kSegD | kSegE | kSegF | kSegG,
      kSegA | kSegB | kSegC,
      kSegA | kSegB | kSegC | kSegD | kSegE | kSegF | kSegG,
      kSegA | kSegB | kSegC | kSegD | kSegF | kSegG,
  };

  uint8_t segments = 0;
  if (symbol >= '0' && symbol <= '9') {
    segments = kDigits[symbol - '0'];
  } else {
    switch (std::toupper(static_cast<unsigned char>(symbol))) {
      case 'A':
        segments = kSegA | kSegB | kSegC | kSegE | kSegF | kSegG;
        break;
      case 'E':
        segments = kSegA | kSegD | kSegE | kSegF | kSegG;
        break;
      case 'F':
        segments = kSegA | kSegE | kSegF | kSegG;
        break;
      case 'H':
        segments = kSegB | kSegC | kSegE | kSegF | kSegG;
        break;
      case 'M':
        segments = kSegA | kSegB | kSegC | kSegE | kSegF;
        break;
      case 'O':
        segments = kSegA | kSegB | kSegC | kSegD | kSegE | kSegF;
        break;
      case 'R':
        segments = kSegA | kSegB | kSegE | kSegF | kSegG;
        break;
      case 'S':
        segments = kSegA | kSegC | kSegD | kSegF | kSegG;
        break;
      case 'T':
        segments = kSegD | kSegE | kSegF | kSegG;
        break;
      case 'U':
      case 'W':
        segments = kSegB | kSegC | kSegD | kSegE | kSegF;
        break;
      default:
        return;
    }
  }

  if ((segments & kSegA) != 0)
    draw_watch_segment(x, y, scale, 0, display_color);
  if ((segments & kSegB) != 0)
    draw_watch_segment(x, y, scale, 1, display_color);
  if ((segments & kSegC) != 0)
    draw_watch_segment(x, y, scale, 2, display_color);
  if ((segments & kSegD) != 0)
    draw_watch_segment(x, y, scale, 3, display_color);
  if ((segments & kSegE) != 0)
    draw_watch_segment(x, y, scale, 4, display_color);
  if ((segments & kSegF) != 0)
    draw_watch_segment(x, y, scale, 5, display_color);
  if ((segments & kSegG) != 0)
    draw_watch_segment(x, y, scale, 6, display_color);
}

void NativeDisplay::draw_watch_day_label(int center_x, int y, int scale, const char *text, int display_color) {
  const int glyph_w = 20 * scale;
  const int gap = std::max(2, 2 * scale);
  const int count = text == nullptr ? 0 : static_cast<int>(std::strlen(text));
  if (count <= 0)
    return;
  const int total_w = glyph_w * count + gap * (count - 1);
  int x = center_x - total_w / 2;
  for (int i = 0; i < count; i++) {
    draw_watch_day_symbol(x, y, scale, text[i], i == 0, display_color);
    x += glyph_w + gap;
  }
}

void NativeDisplay::draw_watch_day_symbol(int x, int y, int scale, char symbol, bool first_letter,
                                          int display_color) {
  constexpr uint16_t kSegA = 1U << 0;
  constexpr uint16_t kSegB = 1U << 1;
  constexpr uint16_t kSegC = 1U << 2;
  constexpr uint16_t kSegD = 1U << 3;
  constexpr uint16_t kSegE = 1U << 4;
  constexpr uint16_t kSegF = 1U << 5;
  constexpr uint16_t kSegG = 1U << 6;
  constexpr uint16_t kSegH = 1U << 7;
  constexpr uint16_t kSegI = 1U << 8;

  uint16_t segments = 0;
  switch (std::toupper(static_cast<unsigned char>(symbol))) {
    case 'A':
      segments = kSegA | kSegB | kSegC | kSegE | kSegF | kSegG;
      break;
    case 'E':
      segments = kSegA | kSegD | kSegE | kSegF | kSegG;
      break;
    case 'F':
      segments = kSegA | kSegE | kSegF | kSegG;
      break;
    case 'H':
      segments = kSegB | kSegC | kSegE | kSegF | kSegG;
      break;
    case 'M':
      segments = first_letter ? (kSegA | kSegB | kSegC | kSegE | kSegF | kSegH | kSegI)
                              : (kSegA | kSegB | kSegC | kSegE | kSegF);
      break;
    case 'O':
      segments = kSegA | kSegB | kSegC | kSegD | kSegE | kSegF;
      break;
    case 'R':
      segments = kSegA | kSegB | kSegC | kSegE | kSegF | kSegG | (first_letter ? 0 : kSegH);
      break;
    case 'S':
      segments = kSegA | kSegC | kSegD | kSegF | kSegG;
      break;
    case 'T':
      segments = first_letter ? (kSegA | kSegH | kSegI) : (kSegA | kSegE | kSegF | kSegH);
      break;
    case 'U':
      segments = kSegB | kSegC | kSegD | kSegE | kSegF;
      break;
    case 'W':
      segments = first_letter ? (kSegB | kSegC | kSegD | kSegE | kSegF | kSegH | kSegI)
                              : (kSegB | kSegC | kSegD | kSegE | kSegF);
      break;
    default:
      return;
  }

  for (int segment = 0; segment < 7; segment++) {
    if ((segments & (1U << segment)) != 0)
      draw_watch_segment(x, y, scale, segment, display_color);
  }
  if ((segments & kSegH) != 0)
    draw_watch_mode_segment(x, y, scale, 0, display_color);
  if ((segments & kSegI) != 0)
    draw_watch_mode_segment(x, y, scale, 1, display_color);
}

void NativeDisplay::draw_watch_colon(int x, int y, int scale, int display_color) {
  const int radius = std::max(3, (22 * scale + 5) / 10);
  const int cx = x + 2 * scale;
  auto draw_dot = [&](int center_y) {
    for (int yy = -radius; yy <= radius; yy++) {
      int x_extent = 0;
      while ((x_extent + 1) * (x_extent + 1) + yy * yy <= radius * radius)
        x_extent++;
      rect_display_color(cx - x_extent, center_y + yy, x_extent * 2 + 1, 1, display_color);
    }
  };
  draw_dot(y + 10 * scale);
  draw_dot(y + 26 * scale);
}

void NativeDisplay::draw_watch_segment(int x, int y, int scale, int segment, int display_color) {
  if (scale <= 0 || segment < 0 || segment >= static_cast<int>(sizeof(kWatchSegments) / sizeof(kWatchSegments[0])))
    return;

  const WatchSegmentShape &shape = kWatchSegments[segment];
  if (shape.points == nullptr || shape.count <= 0)
    return;

  int points[24][2] = {};
  const int count = std::min<int>(shape.count, static_cast<int>(sizeof(points) / sizeof(points[0])));
  for (int i = 0; i < count; i++) {
    points[i][0] = x + (shape.points[i][0] * scale + 5) / 10;
    points[i][1] = y + (shape.points[i][1] * scale + 5) / 10;
  }
  draw_watch_polygon(points, count, display_color);
}

void NativeDisplay::draw_watch_mode_segment(int x, int y, int scale, int segment, int display_color) {
  if (scale <= 0 ||
      segment < 0 ||
      segment >= static_cast<int>(sizeof(kWatchModeSegments) / sizeof(kWatchModeSegments[0]))) {
    return;
  }

  const WatchSegmentShape &shape = kWatchModeSegments[segment];
  if (shape.points == nullptr || shape.count <= 0)
    return;

  int points[8][2] = {};
  const int count = std::min<int>(shape.count, static_cast<int>(sizeof(points) / sizeof(points[0])));
  for (int i = 0; i < count; i++) {
    points[i][0] = x + (shape.points[i][0] * scale + 5) / 10;
    points[i][1] = y + (shape.points[i][1] * scale + 5) / 10;
  }
  draw_watch_polygon(points, count, display_color);
}

void NativeDisplay::draw_watch_polygon(const int points[][2], int count, int display_color) {
  if (points == nullptr || count < 3)
    return;

  int min_y = points[0][1];
  int max_y = points[0][1];
  for (int i = 1; i < count; i++) {
    min_y = std::min(min_y, points[i][1]);
    max_y = std::max(max_y, points[i][1]);
  }

  for (int y = min_y; y <= max_y; y++) {
    int nodes[32] = {};
    int node_count = 0;
    int j = count - 1;
    for (int i = 0; i < count; i++) {
      const int yi = points[i][1];
      const int yj = points[j][1];
      if (((yi <= y) && (yj > y)) || ((yj <= y) && (yi > y))) {
        const int xi = points[i][0];
        const int xj = points[j][0];
        if (node_count < static_cast<int>(sizeof(nodes) / sizeof(nodes[0])))
          nodes[node_count++] = xi + (y - yi) * (xj - xi) / (yj - yi);
      }
      j = i;
    }

    std::sort(nodes, nodes + node_count);
    for (int i = 0; i + 1 < node_count; i += 2) {
      const int x_start = nodes[i];
      const int x_end = nodes[i + 1];
      if (x_end >= x_start)
        rect_display_color(x_start, y, x_end - x_start + 1, 1, display_color);
    }
  }
}

void NativeDisplay::stroke_rect(int x, int y, int width, int height, int thickness, Color color) {
  if (width <= 0 || height <= 0 || thickness <= 0)
    return;
  rect(x, y, width, thickness, color);
  rect(x, y + height - thickness, width, thickness, color);
  rect(x, y, thickness, height, color);
  rect(x + width - thickness, y, thickness, height, color);
}

void NativeDisplay::draw_text_centered(int center_x, int y, const assets::Font &font, int display_color,
                                       const char *text) {
  draw_text_display_color(center_x - text_width(font, text) / 2, y, font, display_color, text);
}

void NativeDisplay::draw_text(int x, int y, const assets::Font &font, Color color, const char *text,
                              TextAlign align) {
  if (align == TextAlign::Right)
    x -= text_width(font, text);

  size_t offset = 0;
  if (text == nullptr)
    return;
  while (text[offset] != '\0') {
    const uint32_t codepoint = decode_utf8_z(text, offset);
    const auto *glyph = find_glyph(font, codepoint);
    if (glyph == nullptr)
      continue;

    const int row_bytes = (glyph->width + 7) / 8;
    for (int yy = 0; yy < glyph->height; yy++) {
      for (int xx = 0; xx < glyph->width; xx++) {
        const uint8_t value = font.bitmap[glyph->bitmap_offset + yy * row_bytes + (xx / 8)];
        if ((value & (0x80 >> (xx % 8))) != 0)
          logical_pixel(x + glyph->x_offset + xx, y + glyph->y_offset + yy, color);
      }
    }
    x += glyph->x_advance;
  }
}

void NativeDisplay::draw_text_display_color(int x, int y, const assets::Font &font, int display_color, const char *text,
                                            TextAlign align) {
  if (align == TextAlign::Right)
    x -= text_width(font, text);

  size_t offset = 0;
  if (text == nullptr)
    return;
  while (text[offset] != '\0') {
    const uint32_t codepoint = decode_utf8_z(text, offset);
    const auto *glyph = find_glyph(font, codepoint);
    if (glyph == nullptr)
      continue;

    const int row_bytes = (glyph->width + 7) / 8;
    for (int yy = 0; yy < glyph->height; yy++) {
      for (int xx = 0; xx < glyph->width; xx++) {
        const uint8_t value = font.bitmap[glyph->bitmap_offset + yy * row_bytes + (xx / 8)];
        if ((value & (0x80 >> (xx % 8))) != 0) {
          const int pixel_x = x + glyph->x_offset + xx;
          const int pixel_y = y + glyph->y_offset + yy;
          logical_pixel(pixel_x, pixel_y, display_color_pixel(display_color, pixel_x, pixel_y));
        }
      }
    }
    x += glyph->x_advance;
  }
}

int NativeDisplay::draw_wrapped_text(int x, int y, int max_width, int line_height, int max_lines,
                                     const assets::Font &font, Color color, const char *text, TextAlign align) {
  if (max_width <= 0 || max_lines <= 0)
    return y;

  bool fitted = false;
  const auto lines = wrap_text(text, font, max_width, max_lines, fitted);
  for (size_t i = 0; i < lines.size(); i++)
    draw_text(x, y + static_cast<int>(i) * line_height, font, color, lines[i].text.c_str(), align);
  return y + static_cast<int>(lines.size()) * line_height;
}

int NativeDisplay::text_width(const assets::Font &font, const char *text) const {
  int width = 0;
  size_t offset = 0;
  if (text == nullptr)
    return width;
  while (text[offset] != '\0') {
    const uint32_t codepoint = decode_utf8_z(text, offset);
    const auto *glyph = find_glyph(font, codepoint);
    if (glyph != nullptr)
      width += glyph->x_advance;
  }
  return width;
}

int NativeDisplay::text_visual_bottom(const assets::Font &font, const char *text) const {
  int bottom = 0;
  size_t offset = 0;
  if (text == nullptr)
    return font.line_height;
  while (text[offset] != '\0') {
    const uint32_t codepoint = decode_utf8_z(text, offset);
    const auto *glyph = find_glyph(font, codepoint);
    if (glyph != nullptr)
      bottom = std::max<int>(bottom, glyph->y_offset + glyph->height);
  }
  return bottom > 0 ? bottom : font.line_height;
}

int NativeDisplay::wrapped_block_height(const std::vector<WrappedLine> &lines, const assets::Font &font,
                                        int line_height) const {
  if (lines.empty())
    return 0;
  const int last_line = static_cast<int>(lines.size()) - 1;
  return last_line * line_height + text_visual_bottom(font, lines.back().text.c_str());
}

std::vector<NativeDisplay::WrappedLine> NativeDisplay::wrap_text(const char *raw, const assets::Font &font,
                                                                 int max_width, int max_lines, bool &fitted) const {
  std::vector<WrappedLine> lines;
  std::string text(raw == nullptr ? "" : raw);
  std::string current;
  size_t current_start = 0;
  size_t current_end = 0;
  size_t pos = 0;
  fitted = true;

  auto push_line = [&](std::string line, size_t source_start, size_t source_end) {
    if (line.empty())
      return true;
    lines.push_back({std::move(line), source_start, source_end > source_start ? source_end - source_start : 0});
    if (static_cast<int>(lines.size()) >= max_lines) {
      fitted = false;
      return false;
    }
    return true;
  };

  auto add_oversized_word = [&](std::string word, size_t word_start) {
    size_t source_start = word_start;
    while (!word.empty()) {
      if (text_width(font, word.c_str()) <= max_width) {
        current = std::move(word);
        current_start = source_start;
        current_end = source_start + current.size();
        return true;
      }

      size_t offset = 0;
      size_t last_fit = 0;
      size_t preferred_break = 0;
      while (offset < word.size()) {
        size_t next = offset;
        const uint32_t codepoint = decode_utf8(word.c_str(), word.size(), next);
        if (next <= offset)
          next = next_utf8_offset(word, offset);
        const std::string candidate = word.substr(0, next);
        if (text_width(font, candidate.c_str()) > max_width)
          break;
        last_fit = next;
        if (preferred_wrap_codepoint(codepoint))
          preferred_break = next;
        offset = next;
      }

      size_t break_at = preferred_break ? preferred_break : last_fit;
      if (break_at == 0)
        break_at = next_utf8_offset(word, 0);
      if (!push_line(word.substr(0, break_at), source_start, source_start + break_at))
        return false;
      word.erase(0, break_at);
      source_start += break_at;
    }
    return true;
  };

  while (pos < text.size()) {
    while (pos < text.size() && wrap_whitespace(text[pos]))
      pos++;
    if (pos >= text.size())
      break;
    size_t end = pos;
    while (end < text.size() && !wrap_whitespace(text[end]))
      end++;
    const std::string word = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (text_width(font, candidate.c_str()) <= max_width) {
      current = candidate;
      if (current.size() == word.size())
        current_start = pos;
      current_end = end;
    } else if (current.empty()) {
      if (!add_oversized_word(word, pos))
        break;
    } else {
      if (!push_line(std::move(current), current_start, current_end))
        break;
      current.clear();
      if (text_width(font, word.c_str()) <= max_width) {
        current = word;
        current_start = pos;
        current_end = end;
      } else if (!add_oversized_word(word, pos)) {
        break;
      }
    }
    pos = end;
  }
  if (!current.empty() && static_cast<int>(lines.size()) < max_lines) {
    lines.push_back({current, current_start, current_end > current_start ? current_end - current_start : 0});
  } else if (!current.empty()) {
    fitted = false;
  }
  if (!fitted && !lines.empty()) {
    auto &line = lines.back();
    line.text = ellipsize(line.text, font, max_width);
    const size_t visible_source = line.text.size() >= 3 ? line.text.size() - 3 : line.text.size();
    line.source_length = std::min(line.source_length, visible_source);
  }
  return lines;
}

std::string NativeDisplay::ellipsize(std::string text, const assets::Font &font, int max_width) const {
  const std::string suffix = "...";
  while (!text.empty() && text_width(font, (text + suffix).c_str()) > max_width)
    remove_last_utf8_char(text);
  return text + suffix;
}

void NativeDisplay::draw_quote_line(int x, int y, int line_height, const assets::Font &font, const WrappedLine &line,
                                    bool highlight_enabled, uint16_t highlight_offset, uint16_t highlight_length,
                                    int highlight_color, int highlight_text_color, int base_text_color) {
  if (!highlight_enabled || highlight_length == 0 || line.text.empty() || line.source_length == 0) {
    draw_text_display_color(x, y, font, base_text_color, line.text.c_str());
    return;
  }

  const size_t line_start = line.source_offset;
  const size_t line_end = line.source_offset + line.source_length;
  const size_t highlight_start = highlight_offset;
  const size_t highlight_end = highlight_start + highlight_length;
  const size_t start = std::max(line_start, highlight_start);
  const size_t end = std::min(line_end, highlight_end);
  if (start >= end) {
    draw_text_display_color(x, y, font, base_text_color, line.text.c_str());
    return;
  }

  const size_t relative_start = std::min(start - line_start, line.text.size());
  const size_t relative_end = std::min(end - line_start, line.text.size());
  if (relative_start >= relative_end) {
    draw_text_display_color(x, y, font, base_text_color, line.text.c_str());
    return;
  }

  const std::string before = line.text.substr(0, relative_start);
  const std::string marked = line.text.substr(relative_start, relative_end - relative_start);
  const std::string after = line.text.substr(relative_end);
  const int highlight_x = x + text_width(font, before.c_str()) - 3;
  const int highlight_width = text_width(font, marked.c_str()) + 6;
  highlight_rect(highlight_x, y + 4, highlight_width, std::max(8, line_height - 8), highlight_color);

  const int marked_x = x + text_width(font, before.c_str());
  const int after_x = marked_x + text_width(font, marked.c_str());
  draw_text_display_color(x, y, font, base_text_color, before.c_str());
  draw_text_display_color(marked_x, y, font, highlight_text_color, marked.c_str());
  draw_text_display_color(after_x, y, font, base_text_color, after.c_str());
}

void NativeDisplay::highlight_rect(int x, int y, int width, int height, int highlight_color) {
  for (int yy = y; yy < y + height; yy++) {
    for (int xx = x; xx < x + width; xx++) {
      logical_pixel(xx, yy, display_color_pixel(highlight_color, xx, yy));
    }
  }
}

NativeDisplay::Color NativeDisplay::display_color_pixel(int display_color, int x, int y) const {
  switch (display_color) {
    case kHighlightRed:
      return Color::Red;
    case kHighlightGrey:
      return ((x + y) & 1) == 0 ? Color::Black : Color::White;
    case kHighlightLightRed:
      return ((x + y) & 1) == 0 ? Color::Red : Color::White;
    case kHighlightDarkRed:
      return ((x + y) & 1) == 0 ? Color::Red : Color::Black;
    case kHighlightLightYellow:
      return ((x + y) & 1) == 0 ? Color::Yellow : Color::White;
    case kHighlightDarkYellow:
      return ((x + y) & 1) == 0 ? Color::Yellow : Color::Black;
    case kHighlightOrange:
      return ((x + y) & 1) == 0 ? Color::Red : Color::Yellow;
    case kHighlightBlack:
      return Color::Black;
    case kHighlightWhite:
      return Color::White;
    case kHighlightYellow:
    default:
      return Color::Yellow;
  }
}

void NativeDisplay::fill(Color color) {
  const uint8_t c = static_cast<uint8_t>(color);
  _buffer.fill(c | (c << 2) | (c << 4) | (c << 6));
}

void NativeDisplay::rect(int x, int y, int width, int height, Color color) {
  for (int yy = y; yy < y + height; yy++) {
    for (int xx = x; xx < x + width; xx++) {
      logical_pixel(xx, yy, color);
    }
  }
}

void NativeDisplay::rect_display_color(int x, int y, int width, int height, int display_color) {
  for (int yy = y; yy < y + height; yy++) {
    for (int xx = x; xx < x + width; xx++) {
      logical_pixel(xx, yy, display_color_pixel(display_color, xx, yy));
    }
  }
}

void NativeDisplay::logical_pixel(int x, int y, Color color) {
  if (logical_to_physical(x, y))
    pixel(x, y, color);
}

void NativeDisplay::pixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight)
    return;
  const uint32_t pixel_position = x + y * kWidth;
  const uint32_t byte_position = pixel_position / kPixelsPerByte;
  const uint32_t bit_offset = 6 - ((pixel_position % kPixelsPerByte) * 2);
  const uint8_t bits = static_cast<uint8_t>(color);
  const uint8_t mask = static_cast<uint8_t>(0b11 << bit_offset);
  _buffer[byte_position] = static_cast<uint8_t>((_buffer[byte_position] & ~mask) | (bits << bit_offset));
}

}  // namespace quotes_clock
