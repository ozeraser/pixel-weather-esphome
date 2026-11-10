#include "pixel_weather.h"
#include "pw_extra.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_gatt_common_api.h>
#include <esp_gattc_api.h>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::pixel_weather {

static const char *const TAG = "pixel_weather";

static const uint16_t SVC_SHORT = 0x00FA;   // 000000fa-0000-1000-8000-00805f9b34fb
static const uint16_t WR_SHORT = 0xFA02;    // write
static const uint16_t NT_SHORT = 0xFA03;    // notify

static const uint32_t CONNECT_TIMEOUT_MS = 30000;
static const uint32_t CONNECT_SETTLE_MS = 1000;     // let the panel settle after connect
static const uint32_t WRITE_TIMEOUT_MS = 5000;
static const uint32_t REPLY_TIMEOUT_MS = 8000;      // per 4 KB chunk, as in the Arduino sketch
static const uint32_t STORE_SETTLE_MS = 3000;       // the fix for lost uploads: wait before dropping the link
static const uint32_t DISCONNECT_TIMEOUT_MS = 15000;
static const uint32_t RETRY_MS = 60000;
static const uint32_t NO_DATA_RETRY_MS = 30000;
static const size_t MESSAGE_MAX = 24;
static const uint8_t BEEP_ALARM_SLOT = 0;           // slot 0 beeps; slot 9 was accepted but never went off (2026-11-10)
static const uint32_t AUTO_BEEP_GAP_MS = 10 * 60 * 1000;   // at most one lightning beep per 10 minutes

// 16-bit short form of a UUID, or 0 if it isn't based on the Bluetooth base UUID
static uint16_t short_uuid(const esp_bt_uuid_t &u) {
  if (u.len == ESP_UUID_LEN_16)
    return u.uuid.uuid16;
  if (u.len == ESP_UUID_LEN_32)
    return u.uuid.uuid32 <= 0xFFFF ? (uint16_t) u.uuid.uuid32 : 0;
  if (u.len == ESP_UUID_LEN_128) {
    static const uint8_t BASE[12] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00};
    if (memcmp(u.uuid.uuid128, BASE, 12) != 0 || u.uuid.uuid128[14] || u.uuid.uuid128[15])
      return 0;
    return u.uuid.uuid128[12] | (u.uuid.uuid128[13] << 8);
  }
  return 0;
}

// "2026-11-01T11:32:03+00:00" (or with fractional seconds / Z) -> Unix time
static bool parse_iso_utc(const std::string &s, int64_t &out) {
  int y, mo, d, h, mi, se;
  if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6)
    return false;
  int off_min = 0;
  size_t tz = s.find_first_of("+-Z", 19);
  if (tz != std::string::npos && s[tz] != 'Z') {
    int oh = 0, om = 0;
    if (sscanf(s.c_str() + tz + 1, "%d:%d", &oh, &om) >= 1)
      off_min = (oh * 60 + om) * (s[tz] == '-' ? -1 : 1);
  }
  // days since 1970-01-01 (Howard Hinnant's days_from_civil)
  y -= mo <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  out = days * 86400 + h * 3600 + mi * 60 + se - off_min * 60;
  return true;
}

// ------------------------------------------------------------- lifecycle ----
void PixelWeather::setup() {
  // first upload shortly after boot, once Home Assistant has sent the sensor values
  this->want_upload_ = true;
  this->not_before_ = millis() + 20000;
}

void PixelWeather::update() { this->want_upload_ = true; }

void PixelWeather::dump_config() {
  ESP_LOGCONFIG(TAG, "Pixel Weather:");
  ESP_LOGCONFIG(TAG, "  Brightness: %u%%  Panel on: %s", this->brightness_, YESNO(this->panel_on_));
  ESP_LOGCONFIG(TAG, "  Lightning warning: within %.0f km, last %.0f min", this->lightning_max_km_,
                this->lightning_minutes_);
  LOG_UPDATE_INTERVAL(this);
}

// -------------------------------------------------------------- controls ----
void PixelWeather::set_brightness(float pct) {
  if (std::isnan(pct))
    return;
  int b = (int) lroundf(pct);
  b = b < 5 ? 5 : (b > 100 ? 100 : b);
  if (b == this->brightness_)
    return;
  this->brightness_ = (uint8_t) b;
  this->want_settings_ = true;
  this->not_before_ = millis() + 1500;   // wait for the slider to stop moving
}

void PixelWeather::set_panel_on(bool on) {
  if (on == this->panel_on_)
    return;
  this->panel_on_ = on;
  this->want_settings_ = true;
  if (on)
    this->want_upload_ = true;
}

void PixelWeather::set_message(const std::string &msg) {
  std::string m = msg.substr(0, MESSAGE_MAX);
  if (m == this->message_)
    return;
  this->message_ = m;
  this->want_upload_ = true;
  this->not_before_ = millis();
}

void PixelWeather::refresh() {
  this->want_upload_ = true;
  this->not_before_ = millis();
}

void PixelWeather::beep() {
  this->want_beep_ = true;
  this->not_before_ = millis();
}

// ----------------------------------------------------------------- content ----
bool PixelWeather::check_lightning_(int &km) {
  km = -1;
  if (this->time_ == nullptr || this->last_strike_ == nullptr || this->strike_km_ == nullptr)
    return false;
  if (!this->last_strike_->has_state() || !this->strike_km_->has_state() || std::isnan(this->strike_km_->state))
    return false;
  ESPTime now = this->time_->now();
  int64_t when;
  if (!now.is_valid() || !parse_iso_utc(this->last_strike_->state, when))
    return false;
  int64_t age = (int64_t) now.timestamp - when;
  float dist = this->strike_km_->state;
  km = (int) lroundf(dist);
  return age >= -60 && age <= (int64_t) (this->lightning_minutes_ * 60) && dist <= this->lightning_max_km_;
}

bool PixelWeather::build_gif_(std::vector<uint8_t> &gif) {
  std::vector<Frame> frames;
  std::vector<uint16_t> delays;
  if (!this->message_.empty()) {
    buildMessageFrames(this->message_, frames, delays);
    this->showing_ = "message";
  } else if (this->lightning_shown_) {
    buildLightningFrames(this->lightning_km_shown_, frames, delays);
    this->showing_ = "lightning";
  } else {
    if (this->temperature_ == nullptr || !this->temperature_->has_state() || std::isnan(this->temperature_->state))
      return false;
    Weather w{};
    w.temp = this->temperature_->state;
    std::string cond = this->condition_ != nullptr && this->condition_->has_state() ? this->condition_->state : "";
    w.code = haConditionCode(cond);
    if (this->sun_ != nullptr && this->sun_->has_state())
      w.isDay = this->sun_->state == "above_horizon";
    else
      w.isDay = cond != "clear-night";
    auto val = [&](sensor::Sensor *s) {
      return s != nullptr && s->has_state() && !std::isnan(s->state) ? s->state : w.temp;
    };
    w.hi = val(this->high_);
    w.lo = val(this->low_);
    if (this->rain_ != nullptr && this->rain_->has_state()) {   // "0,5,10,..." next 16 hours
      const char *p = this->rain_->state.c_str();
      for (int k = 0; k < 16 && *p; k++) {
        int v = atoi(p);
        w.rain[k] = (uint8_t) (v < 0 ? 0 : (v > 100 ? 100 : v));
        p = strchr(p, ',');
        if (p == nullptr)
          break;
        p++;
      }
    }
    int uv = this->uv_ != nullptr && this->uv_->has_state() && !std::isnan(this->uv_->state)
                 ? (int) lroundf(this->uv_->state) : 0;
    buildWeatherFrames(w, uv, frames, delays);
    this->showing_ = "weather";
    ESP_LOGI(TAG, "Weather: %.1f C, %s (code %d, %s), H%.0f L%.0f, UV %d", w.temp, cond.c_str(), w.code,
             w.isDay ? "day" : "night", w.hi, w.lo, uv);
  }
  gif = encodeGif(frames.data(), delays.data(), (int) frames.size());
  ESP_LOGI(TAG, "Showing %s: %u frames, GIF %u bytes", this->showing_.c_str(), (unsigned) frames.size(),
           (unsigned) gif.size());
  return true;
}

// -------------------------------------------------------------- main loop ----
void PixelWeather::goto_(Step s) {
  this->step_ = s;
  this->step_started_ = millis();
}

void PixelWeather::loop() {
  const uint32_t now = millis();

  // lightning check every 10 s; a change starts an upload straight away
  if (now - this->last_lightning_check_ > 10000) {
    this->last_lightning_check_ = now;
    int km;
    bool active = this->check_lightning_(km);
    bool changed = active != this->lightning_shown_ ||
                   (active && std::abs(km - this->lightning_km_shown_) >= 3);
    if (changed) {
      ESP_LOGI(TAG, active ? "Lightning warning: strike %d km away" : "Lightning warning cleared", km);
      if (active && !this->lightning_shown_ && this->beep_enabled_ &&
          (this->last_auto_beep_ == 0 || now - this->last_auto_beep_ > AUTO_BEEP_GAP_MS)) {
        this->want_beep_ = true;
        this->last_auto_beep_ = now;
      }
      this->lightning_shown_ = active;
      this->lightning_km_shown_ = active ? km : -1;
      this->want_upload_ = true;
      this->not_before_ = now;
    }
  }

  if (this->step_ != Step::IDLE && this->step_ != Step::DISCONNECTING && this->failed_) {
    this->finish_(false, this->fail_reason_);
    return;
  }

  const uint32_t elapsed = now - this->step_started_;
  switch (this->step_) {
    case Step::IDLE:
      if ((this->want_upload_ || this->want_settings_ || this->want_beep_) && (int32_t) (now - this->not_before_) >= 0 &&
          this->parent() != nullptr && this->parent()->state() == esp32_ble_tracker::ClientState::IDLE)
        this->start_session_();
      break;

    case Step::CONNECTING:
      if (this->ready_) {
        ESP_LOGD(TAG, "Panel ready (MTU %u)", this->mtu_);
        this->goto_(Step::SETTLE);
      } else if (elapsed > CONNECT_TIMEOUT_MS) {
        this->finish_(false, "panel not found (phone app connected?)");
      }
      break;

    case Step::SETTLE:
      if (elapsed > CONNECT_SETTLE_MS) {
        this->msg_i_ = 0;
        this->msg_off_ = 0;
        this->replied_ = false;
        this->goto_(Step::SENDING);
      }
      break;

    case Step::SENDING:
      if (this->write_pending_) {
        if (elapsed > WRITE_TIMEOUT_MS)
          this->finish_(false, "write timed out");
        break;
      }
      if (this->msg_off_ < this->msgs_[this->msg_i_].data.size()) {
        this->send_next_piece_();
      } else if (this->msgs_[this->msg_i_].wait_reply) {
        this->goto_(Step::WAIT_REPLY);
      } else {
        this->goto_(Step::WAIT_REPLY);
        this->replied_ = true;   // commands don't wait for a reply
      }
      break;

    case Step::WAIT_REPLY:
      if (this->replied_ || elapsed > REPLY_TIMEOUT_MS) {
        if (!this->replied_)
          ESP_LOGW(TAG, "No reply for chunk %u", (unsigned) this->msg_i_);
        this->msg_i_++;
        this->msg_off_ = 0;
        this->replied_ = false;
        this->goto_(this->msg_i_ < this->msgs_.size() ? Step::SENDING : Step::FINAL_SETTLE);
      }
      break;

    case Step::FINAL_SETTLE:
      if (elapsed > (this->session_upload_ || this->session_beep_ ? STORE_SETTLE_MS : 500))
        this->finish_(true, "");
      break;

    case Step::DISCONNECTING:
      if (this->parent()->state() == esp32_ble_tracker::ClientState::IDLE || elapsed > DISCONNECT_TIMEOUT_MS) {
        this->ready_ = false;
        this->goto_(Step::IDLE);
      }
      break;
  }
}

void PixelWeather::start_session_() {
  this->session_upload_ = this->want_upload_ && this->panel_on_;
  bool settings = this->want_settings_;
  bool beep = this->want_beep_;
  std::vector<uint8_t> gif;
  if (this->session_upload_ && !this->build_gif_(gif)) {
    ESP_LOGD(TAG, "No weather data from Home Assistant yet");
    this->last_result_ = "waiting for weather data";
    this->not_before_ = millis() + NO_DATA_RETRY_MS;
    if (!settings && !beep)
      return;
    this->session_upload_ = false;   // still apply the brightness / on-off change; upload stays pending
  } else {
    this->want_upload_ = false;
  }
  this->want_settings_ = false;
  this->want_beep_ = false;

  this->msgs_.clear();
  if (this->panel_on_) {
    this->msgs_.push_back({{5, 0, 7, 1, 1}, false});                     // screen on
    this->msgs_.push_back({{5, 0, 4, 128, this->brightness_}, false});   // brightness
  } else {
    this->msgs_.push_back({{5, 0, 7, 1, 0}, false});                     // screen off
  }
  if (this->session_upload_)
    for (auto &pkt : idmGifPackets(gif))
      this->msgs_.push_back({std::move(pkt), true});
  this->session_beep_ = false;
  if (beep)
    this->add_beep_msgs_();
  if (this->msgs_.empty())
    return;

  this->ready_ = false;
  this->failed_ = false;
  this->write_pending_ = false;
  this->wr_handle_ = this->nt_handle_ = 0;
  this->mtu_ = 23;
  esp_ble_gatt_set_local_mtu(247);
  ESP_LOGI(TAG, "Connecting to panel (%s%s)", this->session_upload_ ? "upload" : "settings only",
           this->session_beep_ ? " + beep" : "");
  this->goto_(Step::CONNECTING);
  this->parent()->connect();
}

// The panel has no plain beep command, so do what the official app does for an alarm: set the
// panel's clock to the real time, then set a one-off alarm (buzzer on, showing the lightning
// animation) for the next whole minute that is at least 10 s away.
void PixelWeather::add_beep_msgs_() {
  if (this->time_ == nullptr || !this->time_->now().is_valid()) {
    ESP_LOGW(TAG, "Beep skipped: time not known yet");
    return;
  }
  ESPTime now = this->time_->now();
  ESPTime at = ESPTime::from_epoch_local(now.timestamp + 70);   // its hour:minute is 10-70 s ahead
  int km = this->lightning_km_shown_;
  if (km < 0 && this->strike_km_ != nullptr && this->strike_km_->has_state() && !std::isnan(this->strike_km_->state))
    km = (int) lroundf(this->strike_km_->state);
  std::vector<Frame> frames;
  std::vector<uint16_t> delays;
  buildLightningFrames(km, frames, delays);
  std::vector<uint8_t> gif = encodeGif(frames.data(), delays.data(), (int) frames.size());
  int weekday = now.day_of_week == 1 ? 7 : now.day_of_week - 1;   // ESPTime: 1 = Sunday; panel: 1 = Monday
  this->msgs_.push_back(
      {idmTimeSync(now.year, now.month, now.day_of_month, weekday, now.hour, now.minute, now.second), false});
  for (auto &pkt : idmAlarmPackets(BEEP_ALARM_SLOT, at.hour, at.minute, this->beep_seconds_, gif))
    this->msgs_.push_back({std::move(pkt), true});
  this->session_beep_ = true;
  ESP_LOGI(TAG, "Beep: alarm at %02d:%02d for %d s (GIF %u bytes)", at.hour, at.minute, this->beep_seconds_,
           (unsigned) gif.size());
}

void PixelWeather::send_next_piece_() {
  Msg &m = this->msgs_[this->msg_i_];
  size_t piece = this->mtu_ > 23 ? this->mtu_ - 3 : 20;
  size_t len = std::min(piece, m.data.size() - this->msg_off_);
  esp_err_t err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                           this->wr_handle_, len, m.data.data() + this->msg_off_,
                                           ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    this->finish_(false, "write failed");
    return;
  }
  this->msg_off_ += len;
  this->write_pending_ = true;
  this->step_started_ = millis();
}

void PixelWeather::finish_(bool ok, const std::string &why) {
  if (ok) {
    this->last_result_ = this->session_upload_ ? "ok" : "ok (settings)";
    if (this->session_beep_) {
      this->last_result_ += " + beep";
      // put our picture back once the alarm has finished, in case the panel doesn't
      this->want_upload_ = true;
      this->not_before_ = millis() + (uint32_t) (this->beep_seconds_ + 190) * 1000;   // alarm is up to 70 s away, and the panel ignores new pictures for a while after it
    }
    if (this->session_upload_ && this->time_ != nullptr && this->time_->now().is_valid())
      this->last_upload_time_ = (float) this->time_->now().timestamp;
    ESP_LOGI(TAG, "Panel updated");
  } else {
    this->failures_++;
    this->last_result_ = "failed: " + why;
    if (this->session_upload_)
      this->want_upload_ = true;
    if (this->session_beep_)
      this->want_beep_ = true;
    this->want_settings_ = true;
    this->not_before_ = millis() + RETRY_MS;
    ESP_LOGW(TAG, "Panel update failed: %s (retry in %u s)", why.c_str(), (unsigned) (RETRY_MS / 1000));
  }
  this->msgs_.clear();
  this->msgs_.shrink_to_fit();
  this->failed_ = false;
  this->write_pending_ = false;
  if (this->parent()->state() != esp32_ble_tracker::ClientState::IDLE)
    this->parent()->disconnect();
  this->goto_(Step::DISCONNECTING);
}

// -------------------------------------------------------------- BLE events ----
void PixelWeather::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status != ESP_GATT_OK && this->step_ == Step::CONNECTING) {
        this->failed_ = true;
        this->fail_reason_ = "connect failed (status " + std::to_string(param->open.status) + ")";
      }
      break;

    case ESP_GATTC_CFG_MTU_EVT:
      if (param->cfg_mtu.status == ESP_GATT_OK)
        this->mtu_ = param->cfg_mtu.mtu;
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (this->step_ != Step::CONNECTING)
        break;
      uint16_t conn = param->search_cmpl.conn_id;
      esp_gattc_service_elem_t svcs[16];
      uint16_t count = 16;
      uint16_t start = 0, end = 0;
      if (esp_ble_gattc_get_service(gattc_if, conn, nullptr, svcs, &count, 0) == ESP_GATT_OK) {
        for (uint16_t i = 0; i < count; i++)
          if (short_uuid(svcs[i].uuid) == SVC_SHORT) {
            start = svcs[i].start_handle;
            end = svcs[i].end_handle;
          }
      }
      if (start != 0) {
        esp_gattc_char_elem_t chars[16];
        count = 16;
        if (esp_ble_gattc_get_all_char(gattc_if, conn, start, end, chars, &count, 0) == ESP_GATT_OK) {
          for (uint16_t i = 0; i < count; i++) {
            uint16_t u = short_uuid(chars[i].uuid);
            if (u == WR_SHORT)
              this->wr_handle_ = chars[i].char_handle;
            if (u == NT_SHORT)
              this->nt_handle_ = chars[i].char_handle;
          }
        }
      }
      if (this->wr_handle_ == 0 || this->nt_handle_ == 0) {
        this->failed_ = true;
        this->fail_reason_ = "iDotMatrix service not found";
        this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
        break;
      }
      if (this->parent()->register_for_notify(this->nt_handle_) != ESP_OK) {
        this->failed_ = true;
        this->fail_reason_ = "could not subscribe to panel replies";
        this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle == this->nt_handle_) {
        // services may be released from here on; we only keep the two handles
        this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
        if (param->reg_for_notify.status == ESP_GATT_OK) {
          this->ready_ = true;
        } else {
          this->failed_ = true;
          this->fail_reason_ = "subscribe failed";
        }
      }
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->wr_handle_ && this->write_pending_) {
        this->write_pending_ = false;
        if (param->write.status != ESP_GATT_OK) {
          this->failed_ = true;
          this->fail_reason_ = "panel rejected write (status " + std::to_string(param->write.status) + ")";
        }
      }
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->nt_handle_) {  // braces keep hex[] scoped to this case
        char hex[format_hex_pretty_size(24)];
        format_hex_pretty_to(hex, param->notify.value, std::min<size_t>(param->notify.value_len, 24));
        ESP_LOGD(TAG, "Panel says: %s", hex);
        this->replied_ = true;
      }
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      if (this->step_ != Step::IDLE && this->step_ != Step::DISCONNECTING && this->step_ != Step::FINAL_SETTLE) {
        this->failed_ = true;
        this->fail_reason_ = "panel disconnected";
      }
      this->ready_ = false;
      this->write_pending_ = false;
      break;

    default:
      break;
  }
}

}  // namespace esphome::pixel_weather
