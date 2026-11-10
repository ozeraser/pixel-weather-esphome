#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome::pixel_weather {

// Renders the weather screens and uploads them to an iDotMatrix 16x16 panel over BLE.
// The BLE link is opened only for an upload (or a brightness / on-off change) and closed again,
// so the phone app can still reach the panel in between.
class PixelWeather : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // inputs (set from YAML)
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_ = s; }
  void set_high_sensor(sensor::Sensor *s) { this->high_ = s; }
  void set_low_sensor(sensor::Sensor *s) { this->low_ = s; }
  void set_uv_index_sensor(sensor::Sensor *s) { this->uv_ = s; }
  void set_lightning_distance_sensor(sensor::Sensor *s) { this->strike_km_ = s; }
  void set_condition_text_sensor(text_sensor::TextSensor *s) { this->condition_ = s; }
  void set_rain_forecast_text_sensor(text_sensor::TextSensor *s) { this->rain_ = s; }
  void set_sun_text_sensor(text_sensor::TextSensor *s) { this->sun_ = s; }
  void set_last_lightning_text_sensor(text_sensor::TextSensor *s) { this->last_strike_ = s; }

  // controls (called from the template number / switch / button / text in YAML)
  void set_brightness(float pct);
  void set_panel_on(bool on);
  void set_message(const std::string &msg);
  void set_lightning_max_km(float km) { this->lightning_max_km_ = km; }
  void set_lightning_minutes(float min) { this->lightning_minutes_ = min; }
  void refresh();
  void beep();                                              // sound the panel's buzzer (via its alarm)
  void set_beep_enabled(bool on) { this->beep_enabled_ = on; }   // beep when a lightning warning starts
  void set_beep_seconds(float s) { this->beep_seconds_ = std::isnan(s) ? 10 : (int) s; }

  // status (read by template sensors in YAML)
  bool panel_connected() { return this->parent() != nullptr && this->parent()->connected(); }
  const std::string &last_result() const { return this->last_result_; }
  float last_upload_time() const { return this->last_upload_time_; }
  float failures() const { return (float) this->failures_; }
  bool lightning_active() const { return this->lightning_shown_; }
  const std::string &showing() const { return this->showing_; }

 protected:
  enum class Step : uint8_t { IDLE, CONNECTING, SETTLE, SENDING, WAIT_REPLY, FINAL_SETTLE, DISCONNECTING };
  struct Msg {
    std::vector<uint8_t> data;
    bool wait_reply;
  };

  bool build_gif_(std::vector<uint8_t> &gif);
  void add_beep_msgs_();
  bool check_lightning_(int &km);
  void start_session_();
  void send_next_piece_();
  void finish_(bool ok, const std::string &why);
  void goto_(Step s);

  time::RealTimeClock *time_{nullptr};
  sensor::Sensor *temperature_{nullptr}, *high_{nullptr}, *low_{nullptr}, *uv_{nullptr}, *strike_km_{nullptr};
  text_sensor::TextSensor *condition_{nullptr}, *rain_{nullptr}, *sun_{nullptr}, *last_strike_{nullptr};

  // settings
  uint8_t brightness_{60};
  bool panel_on_{true};
  std::string message_;
  float lightning_max_km_{20};
  float lightning_minutes_{30};
  bool beep_enabled_{true};
  int beep_seconds_{10};

  // what needs doing
  bool want_upload_{false};
  bool want_settings_{false};
  bool want_beep_{false};
  uint32_t last_auto_beep_{0};
  uint32_t not_before_{0};
  uint32_t last_lightning_check_{0};
  bool lightning_shown_{false};
  int lightning_km_shown_{-1};

  // BLE session
  Step step_{Step::IDLE};
  uint32_t step_started_{0};
  bool session_upload_{false};
  bool session_beep_{false};
  bool ready_{false};
  bool write_pending_{false};
  bool replied_{false};
  bool failed_{false};
  std::string fail_reason_;
  uint16_t wr_handle_{0}, nt_handle_{0};
  uint16_t mtu_{23};
  std::vector<Msg> msgs_;
  size_t msg_i_{0}, msg_off_{0};

  // status
  std::string last_result_{"not run yet"};
  std::string showing_{"weather"};
  float last_upload_time_{NAN};
  uint32_t failures_{0};
};

}  // namespace esphome::pixel_weather
