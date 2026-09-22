#include "mq_gas_sensor.h"

#include <algorithm>
#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::mq_gas_sensors {

static const char *const TAG = "mq_gas_sensors";

/// Version tag mixed into the preference key of the persisted R0 value.
static constexpr uint32_t R0_PREFERENCE_VERSION = 0x00000001;

void MQGasSensor::setup() {
  this->r0_pref_ = this->make_entity_preference<float>(R0_PREFERENCE_VERSION);

  if (this->source_ == nullptr) {
    ESP_LOGE(TAG, "'%s %s': no voltage source configured", this->type_.c_str(), this->gas_.c_str());
    this->mark_failed();
    return;
  }

  if (this->r0_configured_) {
    ESP_LOGI(TAG, "'%s %s': using the R0 from the configuration (%.4f kOhm)", this->type_.c_str(),
             this->gas_.c_str(), this->r0_);
  } else if (this->persist_ && this->load_r0_()) {
    ESP_LOGI(TAG, "'%s %s': restored R0 from flash (%.4f kOhm)", this->type_.c_str(),
             this->gas_.c_str(), this->r0_);
  }

  if (this->warmup_time_ > 0) {
    this->warmup_end_ = millis() + this->warmup_time_;
    ESP_LOGI(TAG, "'%s %s': warm-up/burn-in of %" PRIu32 " s - no readings published before that",
             this->type_.c_str(), this->gas_.c_str(), this->warmup_time_ / 1000);
  }

  if (this->calibration_enabled_) {
    if (this->has_r0()) {
      ESP_LOGI(TAG,
               "'%s %s': R0 already known (%.4f kOhm) - automatic calibration skipped, call "
               "request_calibration() (or clear the stored R0) to recalibrate",
               this->type_.c_str(), this->gas_.c_str(), this->r0_);
    } else {
      const uint32_t delay = std::max(this->calibration_delay_, this->warmup_time_);
      this->calibration_due_ = millis() + delay;
      this->calibration_pending_ = true;
      ESP_LOGI(TAG, "'%s %s': R0 calibration scheduled in %" PRIu32
                    " s - the sensor must be in clean air",
               this->type_.c_str(), this->gas_.c_str(), delay / 1000);
    }
  } else if (!this->has_r0()) {
    ESP_LOGW(TAG, "'%s %s': no R0 available, set 'r0:' or 'calibration:' - the PPM value stays "
                  "unknown until then",
             this->type_.c_str(), this->gas_.c_str());
  }
}

void MQGasSensor::log_config_() {
  ESP_LOGCONFIG(TAG, "  Type: %s", this->type_.c_str());
  ESP_LOGCONFIG(TAG, "  Target gas: %s", this->gas_.c_str());
  ESP_LOGCONFIG(TAG, "  Curve: %s (a=%.6g, b=%.6g), ratio: %s",
                this->regression_method_ == mqmath::REGRESSION_EXPONENTIAL ? "exponential"
                                                                          : "linear",
                this->a_, this->b_,
                this->ratio_mode_ == mqmath::RATIO_R0_RS ? "R0/RS (MQUnifiedsensor)"
                                                         : "RS/R0 (datasheet)");
  ESP_LOGCONFIG(TAG, "  VCC: %.2f V, RL: %.2f kOhm, AO multiplier: x%.3f", this->vcc_, this->rl_,
                this->voltage_multiplier_);
  ESP_LOGCONFIG(TAG, "  Range: %.1f - %.1f ppm, samples: %u x %" PRIu32 " ms",
                this->min_ppm_, this->max_ppm_, static_cast<unsigned>(this->samples_),
                this->sample_interval_);
  if (this->has_r0()) {
    ESP_LOGCONFIG(TAG, "  R0: %.4f kOhm (%s)", this->r0_,
                  this->r0_configured_ ? "configured" : "calibrated/restored");
  } else {
    ESP_LOGCONFIG(TAG, "  R0: not available yet");
  }
  ESP_LOGCONFIG(TAG, "  RS/R0 in clean air: %.2f, correction factor: %.4f",
                this->ratio_in_clean_air_, this->correction_factor_);
  ESP_LOGCONFIG(TAG, "  Warm-up: %" PRIu32 " s, auto calibration: %s", this->warmup_time_ / 1000,
                this->calibration_enabled_ ? "enabled" : "disabled");
}

void MQGasSensor::dump_config() {
  LOG_SENSOR("", "MQ gas sensor", this);
  this->log_config_();
}

float MQGasSensor::sample_voltage_() {
  if (this->source_ == nullptr)
    return NAN;

  float sum = 0.0f;
  for (uint8_t i = 0; i < this->samples_; i++) {
    if (i > 0)
      delay(this->sample_interval_);
    sum += this->source_->sample();
  }
  return (sum / static_cast<float>(this->samples_)) * this->voltage_multiplier_;
}

float MQGasSensor::current_rs_() const {
  return mqmath::rs_from_voltage(this->sensor_voltage_, this->vcc_, this->rl_);
}

float MQGasSensor::current_ratio_() const {
  return mqmath::ratio_from_rs(this->rs_, this->r0_, this->correction_factor_,
                              static_cast<mqmath::RatioMode>(this->ratio_mode_));
}

float MQGasSensor::read_ppm_() {
  const float ppm = mqmath::ppm_from_ratio(
      this->a_, this->b_, this->ratio_,
      static_cast<mqmath::RegressionMethod>(this->regression_method_));
  if (!std::isfinite(ppm))
    return NAN;
  return mqmath::clamp_ppm(ppm, this->min_ppm_, this->max_ppm_);
}

void MQGasSensor::publish_diagnostics_() {
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(this->sensor_voltage_);
  if (this->rs_sensor_ != nullptr)
    this->rs_sensor_->publish_state(this->rs_);
  if (this->ratio_sensor_ != nullptr)
    this->ratio_sensor_->publish_state(this->ratio_);
}

void MQGasSensor::update() {
  if (this->is_failed())
    return;

  if (this->calibrating_) {
    ESP_LOGD(TAG, "'%s %s': update skipped, calibration in progress", this->type_.c_str(),
             this->gas_.c_str());
    return;
  }

  if (this->calibration_pending_) {
    this->publish_state(NAN);
    return;
  }

  if (this->warmup_time_ > 0 && millis() < this->warmup_end_) {
    if (!this->warmup_notified_) {
      this->warmup_notified_ = true;
      this->publish_state(NAN);
      ESP_LOGI(TAG, "'%s %s': warming up, %" PRIu32 " s to go", this->type_.c_str(),
               this->gas_.c_str(), (this->warmup_end_ - millis()) / 1000);
    }
    return;
  }

  if (!this->has_r0()) {
    if (!this->warned_no_r0_) {
      this->warned_no_r0_ = true;
      ESP_LOGW(TAG, "'%s %s': R0 unknown - publish 'calibration:' or 'r0:' in the configuration",
               this->type_.c_str(), this->gas_.c_str());
    }
    this->publish_state(NAN);
    return;
  }

  this->sensor_voltage_ = this->sample_voltage_();
  if (!std::isfinite(this->sensor_voltage_) || this->sensor_voltage_ <= 0.01f) {
    if (!this->warned_voltage_) {
      this->warned_voltage_ = true;
      ESP_LOGW(TAG,
               "'%s %s': analog output reads %.4f V - open circuit, missing supply or bad "
               "divider? check the AO wiring (RS would be invalid)",
               this->type_.c_str(), this->gas_.c_str(), this->sensor_voltage_);
    }
    this->rs_ = 0.0f;
    this->ratio_ = 0.0f;
    this->publish_diagnostics_();
    this->publish_state(NAN);
    return;
  }
  this->warned_voltage_ = false;

  this->rs_ = this->current_rs_();
  this->ratio_ = this->current_ratio_();
  const float ppm = this->read_ppm_();

  ESP_LOGD(TAG, "'%s %s': V=%.4f V, RS=%.4f kOhm, ratio=%.4f -> %.1f ppm", this->type_.c_str(),
           this->gas_.c_str(), this->sensor_voltage_, this->rs_, this->ratio_, ppm);

  this->publish_diagnostics_();
  this->publish_state(ppm);
}

void MQGasSensor::set_calibration(bool enabled, float ratio_in_clean_air, uint32_t delay,
                                  uint32_t duration, uint32_t samples, bool persist) {
  this->calibration_enabled_ = enabled;
  this->ratio_in_clean_air_ = ratio_in_clean_air;
  this->calibration_delay_ = delay;
  this->calibration_duration_ = duration;
  this->calibration_samples_ = samples;
  this->persist_ = persist;
}

void MQGasSensor::request_calibration() {
  if (this->calibrating_) {
    ESP_LOGW(TAG, "'%s %s': calibration already running", this->type_.c_str(), this->gas_.c_str());
    return;
  }
  if (!(this->ratio_in_clean_air_ > 0.0f)) {
    ESP_LOGE(TAG, "'%s %s': cannot calibrate without a valid 'ratio_in_clean_air'",
             this->type_.c_str(), this->gas_.c_str());
    return;
  }
  this->calibration_due_ = millis();
  this->calibration_pending_ = true;
}

void MQGasSensor::loop() {
  if (this->is_failed())
    return;

  if (this->calibration_pending_) {
    if (millis() >= this->calibration_due_)
      this->begin_calibration_();
    return;
  }

  if (!this->calibrating_)
    return;

  const uint32_t now = millis();
  if (now - this->calibration_last_sample_ < this->sample_interval_)
    return;

  this->calibration_last_sample_ = now;
  this->process_calibration_();
}

void MQGasSensor::begin_calibration_() {
  this->calibration_pending_ = false;
  this->calibrating_ = true;
  this->calibration_start_ = millis();
  this->calibration_last_sample_ = this->calibration_start_;
  this->calibration_count_ = 0;
  this->calibration_attempts_ = 0;
  this->calibration_sum_ = 0.0f;

  ESP_LOGI(TAG,
           "'%s %s': calibrating R0 (RS/R0 in clean air = %.2f), %" PRIu32
           " samples - keep the sensor in clean air",
           this->type_.c_str(), this->gas_.c_str(), this->ratio_in_clean_air_,
           this->calibration_samples_);
}

void MQGasSensor::process_calibration_() {
  this->calibration_attempts_++;

  const float voltage = this->sample_voltage_();
  if (std::isfinite(voltage) && voltage > 0.01f) {
    this->sensor_voltage_ = voltage;
    this->rs_ = this->current_rs_();
    const float r0 =
        mqmath::r0_from_clean_air(this->rs_, this->ratio_in_clean_air_, this->correction_factor_);
    if (r0 > 0.0f) {
      this->calibration_sum_ += r0;
      this->calibration_count_++;
    }
  }

  const bool samples_done = this->calibration_attempts_ >= this->calibration_samples_;
  const bool timed_out = this->calibration_duration_ > 0 &&
                         (millis() - this->calibration_start_) >= this->calibration_duration_;
  if (samples_done || timed_out)
    this->finish_calibration_();
}

void MQGasSensor::finish_calibration_() {
  this->calibrating_ = false;
  const uint32_t attempts = this->calibration_attempts_;
  const uint32_t valid = this->calibration_count_;

  if (valid == 0) {
    ESP_LOGE(TAG,
             "'%s %s': calibration failed, none of the %" PRIu32
             " samples produced a valid RS - check the AO wiring and the supply",
             this->type_.c_str(), this->gas_.c_str(), attempts);
    return;
  }

  const float r0 = this->calibration_sum_ / static_cast<float>(valid);
  if (!std::isfinite(r0) || r0 <= 0.0f) {
    ESP_LOGE(TAG, "'%s %s': calibration produced an invalid R0 (%.6f)", this->type_.c_str(),
             this->gas_.c_str(), r0);
    return;
  }

  this->r0_ = r0;
  if (this->persist_)
    this->save_r0_();

  ESP_LOGI(TAG, "'%s %s': R0 = %.4f kOhm (%" PRIu32 "/%" PRIu32 " samples valid)%s",
           this->type_.c_str(), this->gas_.c_str(), this->r0_, valid, attempts,
           this->persist_ ? ", stored in flash" : "");
  if (!this->persist_) {
    ESP_LOGI(TAG, "  -> hard-code 'r0: %.4f' in the YAML to skip the calibration at boot",
             this->r0_);
  }
}

void MQGasSensor::save_r0_() {
  if (!this->r0_pref_.save(&this->r0_)) {
    ESP_LOGW(TAG, "'%s %s': could not store R0 in flash", this->type_.c_str(), this->gas_.c_str());
    return;
  }
  if (!global_preferences->sync()) {
    ESP_LOGW(TAG, "'%s %s': R0 stored but the flash sync failed (will be written later)",
             this->type_.c_str(), this->gas_.c_str());
  }
  ESP_LOGD(TAG, "'%s %s': R0 %.4f kOhm saved", this->type_.c_str(), this->gas_.c_str(), this->r0_);
}

bool MQGasSensor::load_r0_() {
  float stored = 0.0f;
  if (!this->r0_pref_.load(&stored))
    return false;
  if (!std::isfinite(stored) || stored <= 0.0f)
    return false;
  this->r0_ = stored;
  return true;
}

}  // namespace esphome::mq_gas_sensors


