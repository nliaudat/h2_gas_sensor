#include "mics_5524_gas_sensor.h"

#include <algorithm>
#include <cinttypes>

#include "esphome/core/log.h"

namespace esphome::mics_5524_gas_sensor {

static const char *const TAG = "mics_5524_gas_sensor";

/// Version tag mixed into the preference key of the persisted calibration value.
/// The conversion model is added to it, so a value stored for one model can
/// never be used by the other one.
static constexpr uint32_t REFERENCE_PREFERENCE_VERSION = 0x00000010;

/// Human readable name of the stored clean-air reference.
static const char *reference_name(uint8_t conversion) {
  return conversion == micsmath::CONVERSION_DFROBOT ? "air reference" : "R0";
}

/// Human readable name of a conversion model.
static const char *conversion_name(uint8_t conversion) {
  return conversion == micsmath::CONVERSION_DFROBOT ? "dfrobot" : "datasheet";
}

void MiCS5524GasSensor::setup() {
  this->reference_pref_ = this->make_entity_preference<float>(REFERENCE_PREFERENCE_VERSION + this->conversion_);

  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    // "enabled": the DFRobot modules are enabled by a LOW level, which the pin's
    // own `inverted: true` configuration takes care of.
    this->enable_pin_->digital_write(true);
    ESP_LOGI(TAG, "'%s': sensor enabled via the EN pin", this->gas_.c_str());
  }

  if (this->source_ == nullptr) {
    ESP_LOGE(TAG, "'%s': no voltage source configured", this->gas_.c_str());
    this->mark_failed();
    return;
  }

  if (this->reference_configured_) {
    ESP_LOGI(TAG, "'%s': using the %s from the configuration (%.4f)", this->gas_.c_str(),
             reference_name(this->conversion_), this->reference_);
  } else if (this->persist_ && this->load_reference_()) {
    ESP_LOGI(TAG, "'%s': restored the %s from flash (%.4f)", this->gas_.c_str(), reference_name(this->conversion_),
             this->reference_);
  }

  if (this->warmup_time_ > 0) {
    this->warmup_end_ = millis() + this->warmup_time_;
    ESP_LOGI(TAG, "'%s': warm-up of %" PRIu32 " s - no readings published before that", this->gas_.c_str(),
             this->warmup_time_ / 1000);
  }

  if (this->calibration_enabled_) {
    if (this->has_reference()) {
      ESP_LOGI(TAG,
               "'%s': %s already known (%.4f) - automatic calibration skipped, call "
               "request_calibration() (or clear the stored value) to recalibrate",
               this->gas_.c_str(), reference_name(this->conversion_), this->reference_);
    } else {
      const uint32_t delay = std::max(this->calibration_delay_, this->warmup_time_);
      this->calibration_due_ = millis() + delay;
      this->calibration_pending_ = true;
      ESP_LOGI(TAG, "'%s': %s calibration scheduled in %" PRIu32 " s - the sensor must be in clean air",
               this->gas_.c_str(), reference_name(this->conversion_), delay / 1000);
    }
  } else if (!this->has_reference()) {
    ESP_LOGW(TAG,
             "'%s': no %s available, set it in the configuration or enable 'calibration:' - "
             "the PPM value stays unknown until then",
             this->gas_.c_str(), reference_name(this->conversion_));
  }
}

void MiCS5524GasSensor::log_config_() {
  ESP_LOGCONFIG(TAG, "  Gas: %s", this->gas_.c_str());
  ESP_LOGCONFIG(TAG, "  Conversion: %s%s", conversion_name(this->conversion_),
                this->is_vendor_model_() ? " (DFRobot_MICS vendor model, no RL needed)" : " (a * (RS/R0)^b)");
  if (this->is_vendor_model_()) {
    ESP_LOGCONFIG(TAG, "  Vendor curve: threshold %.4f, gain %.8f, range %.1f - %.1f ppm", this->threshold_,
                  this->gain_, this->vendor_min_ppm_, this->vendor_max_ppm_);
  } else {
    ESP_LOGCONFIG(TAG, "  Curve: a=%.6g, b=%.6g", this->a_, this->b_);
  }
  ESP_LOGCONFIG(TAG, "  VCC: %.2f V, RL: %.2f kOhm, AO multiplier: x%.3f", this->vcc_, this->rl_,
                this->voltage_multiplier_);
  ESP_LOGCONFIG(TAG, "  Range: %.1f - %.1f ppm, samples: %u x %" PRIu32 " ms", this->min_ppm_, this->max_ppm_,
                static_cast<unsigned>(this->samples_), this->sample_interval_);
  if (this->has_reference()) {
    ESP_LOGCONFIG(TAG, "  %s: %.4f (%s)", reference_name(this->conversion_), this->reference_,
                  this->reference_configured_ ? "configured" : "calibrated/restored");
  } else {
    ESP_LOGCONFIG(TAG, "  %s: not available yet", reference_name(this->conversion_));
  }
  ESP_LOGCONFIG(TAG, "  Warm-up: %" PRIu32 " s, auto calibration: %s, EN pin: %s", this->warmup_time_ / 1000,
                this->calibration_enabled_ ? "enabled" : "disabled",
                this->enable_pin_ != nullptr ? "driven" : "not used");
}

void MiCS5524GasSensor::dump_config() {
  LOG_SENSOR("", "MiCS-5524 gas sensor", this);
  this->log_config_();
}

float MiCS5524GasSensor::sample_voltage_() {
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

float MiCS5524GasSensor::current_rs_() const {
  return micsmath::rs_from_voltage(this->sensor_voltage_, this->vcc_, this->rl_);
}

micsmath::VendorCurve MiCS5524GasSensor::vendor_curve_() const {
  return micsmath::VendorCurve{this->threshold_, this->gain_, this->vendor_min_ppm_, this->vendor_max_ppm_};
}

float MiCS5524GasSensor::current_ratio_() {
  if (this->is_vendor_model_()) {
    this->air_value_ = micsmath::air_value_from_voltage(this->sensor_voltage_, this->vcc_);
    return micsmath::ratio_from_air_value(this->air_value_, this->reference_);
  }
  this->rs_ = this->current_rs_();
  return micsmath::ratio_from_rs(this->rs_, this->reference_);
}

float MiCS5524GasSensor::read_ppm_() {
  const float ppm = this->is_vendor_model_() ? micsmath::ppm_from_vendor_curve(this->ratio_, this->vendor_curve_())
                                             : micsmath::ppm_from_power_law(this->a_, this->b_, this->ratio_);
  return micsmath::clamp_ppm(ppm, this->min_ppm_, this->max_ppm_);
}

void MiCS5524GasSensor::publish_diagnostics_() {
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(this->sensor_voltage_);
  if (this->ratio_sensor_ != nullptr)
    this->ratio_sensor_->publish_state(this->ratio_);
  // RS only exists in the datasheet model (the vendor model never needs it).
  if (this->rs_sensor_ != nullptr && !this->is_vendor_model_())
    this->rs_sensor_->publish_state(this->rs_);
}

void MiCS5524GasSensor::update() {
  if (this->is_failed())
    return;

  if (this->calibrating_) {
    ESP_LOGD(TAG, "'%s': update skipped, calibration in progress", this->gas_.c_str());
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
      ESP_LOGI(TAG, "'%s': warming up, %" PRIu32 " s to go", this->gas_.c_str(), (this->warmup_end_ - millis()) / 1000);
    }
    return;
  }

  if (!this->has_reference()) {
    if (!this->warned_no_reference_) {
      this->warned_no_reference_ = true;
      ESP_LOGW(TAG, "'%s': %s unknown - publish it in the configuration or enable 'calibration:'", this->gas_.c_str(),
               reference_name(this->conversion_));
    }
    this->publish_state(NAN);
    return;
  }

  this->sensor_voltage_ = this->sample_voltage_();
  if (!std::isfinite(this->sensor_voltage_) || this->sensor_voltage_ <= 0.01f) {
    if (!this->warned_voltage_) {
      this->warned_voltage_ = true;
      ESP_LOGW(TAG,
               "'%s': analog output reads %.4f V - open circuit, missing supply or bad "
               "divider? check the AO wiring",
               this->gas_.c_str(), this->sensor_voltage_);
    }
    this->air_value_ = 0.0f;
    this->rs_ = 0.0f;
    this->ratio_ = 0.0f;
    this->publish_diagnostics_();
    this->publish_state(NAN);
    return;
  }
  this->warned_voltage_ = false;

  this->ratio_ = this->current_ratio_();
  const float ppm = this->read_ppm_();

  ESP_LOGD(TAG, "'%s': V_AO=%.4f V, x=%.4f, RS=%.4f kOhm, ratio=%.4f (%s) -> %.1f ppm", this->gas_.c_str(),
           this->sensor_voltage_, this->air_value_, this->rs_, this->ratio_, conversion_name(this->conversion_), ppm);

  this->publish_diagnostics_();
  this->publish_state(ppm);
}

void MiCS5524GasSensor::set_calibration(bool enabled, uint32_t delay, uint32_t samples, bool persist) {
  this->calibration_enabled_ = enabled;
  this->calibration_delay_ = delay;
  this->calibration_samples_ = samples;
  this->persist_ = persist;
}

void MiCS5524GasSensor::set_r0(float r0) {
  this->reference_ = r0;
  this->reference_configured_ = true;
}

void MiCS5524GasSensor::set_air_reference(float air_reference) {
  this->reference_ = air_reference;
  this->reference_configured_ = true;
}

void MiCS5524GasSensor::request_calibration() {
  if (this->calibrating_) {
    ESP_LOGW(TAG, "'%s': calibration already running", this->gas_.c_str());
    return;
  }
  if (this->calibration_samples_ == 0) {
    ESP_LOGE(TAG, "'%s': cannot calibrate without samples", this->gas_.c_str());
    return;
  }
  this->calibration_due_ = millis();
  this->calibration_pending_ = true;
}

void MiCS5524GasSensor::loop() {
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

void MiCS5524GasSensor::begin_calibration_() {
  this->calibration_pending_ = false;
  this->calibrating_ = true;
  this->calibration_start_ = millis();
  this->calibration_last_sample_ = this->calibration_start_;
  this->calibration_count_ = 0;
  this->calibration_attempts_ = 0;
  this->calibration_sum_ = 0.0f;

  ESP_LOGI(TAG, "'%s': calibrating the %s (%s model), %" PRIu32 " samples - keep the sensor in clean air",
           this->gas_.c_str(), reference_name(this->conversion_), conversion_name(this->conversion_),
           this->calibration_samples_);
}

void MiCS5524GasSensor::process_calibration_() {
  this->calibration_attempts_++;

  const float voltage = this->sample_voltage_();
  if (std::isfinite(voltage) && voltage > 0.01f) {
    this->sensor_voltage_ = voltage;
    const float reference =
        this->is_vendor_model_() ? micsmath::air_value_from_voltage(voltage, this->vcc_) : this->current_rs_();
    if (std::isfinite(reference) && reference > 0.0f) {
      this->calibration_sum_ += reference;
      this->calibration_count_++;
    }
  }

  if (this->calibration_attempts_ >= this->calibration_samples_)
    this->finish_calibration_();
}

void MiCS5524GasSensor::finish_calibration_() {
  this->calibrating_ = false;
  const uint32_t attempts = this->calibration_attempts_;
  const uint32_t valid = this->calibration_count_;

  if (valid == 0) {
    ESP_LOGE(TAG,
             "'%s': calibration failed, none of the %" PRIu32
             " samples produced a valid reading - check the AO wiring and the supply",
             this->gas_.c_str(), attempts);
    return;
  }

  const float reference = this->calibration_sum_ / static_cast<float>(valid);
  if (!std::isfinite(reference) || reference <= 0.0f) {
    ESP_LOGE(TAG, "'%s': calibration produced an invalid %s (%.6f)", this->gas_.c_str(),
             reference_name(this->conversion_), reference);
    return;
  }

  this->reference_ = reference;
  this->reference_configured_ = true;
  if (this->persist_)
    this->save_reference_();

  ESP_LOGI(TAG, "'%s': %s = %.4f (%" PRIu32 "/%" PRIu32 " samples valid)%s", this->gas_.c_str(),
           reference_name(this->conversion_), this->reference_, valid, attempts,
           this->persist_ ? ", stored in flash" : "");
  if (!this->persist_) {
    ESP_LOGI(TAG, "  -> hard-code '%s: %.4f' in the YAML to skip the calibration at boot",
             this->is_vendor_model_() ? "air_reference" : "r0", this->reference_);
  }
}

void MiCS5524GasSensor::save_reference_() {
  if (!this->reference_pref_.save(&this->reference_)) {
    ESP_LOGW(TAG, "'%s': could not store the %s in flash", this->gas_.c_str(), reference_name(this->conversion_));
    return;
  }
  if (!global_preferences->sync()) {
    ESP_LOGW(TAG, "'%s': %s stored but the flash sync failed (will be written later)", this->gas_.c_str(),
             reference_name(this->conversion_));
  }
  ESP_LOGD(TAG, "'%s': %s %.4f saved", this->gas_.c_str(), reference_name(this->conversion_), this->reference_);
}

bool MiCS5524GasSensor::load_reference_() {
  float stored = 0.0f;
  if (!this->reference_pref_.load(&stored))
    return false;
  if (!std::isfinite(stored) || stored <= 0.0f)
    return false;
  this->reference_ = stored;
  this->reference_configured_ = true;
  return true;
}

}  // namespace esphome::mics_5524_gas_sensor
