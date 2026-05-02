#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace BMI088Calibration {

enum class StaticCalibrationState : std::uint8_t {
  IDLE,
  RUNNING,
  SUCCEEDED,
  FAILED
};

enum class StaticCalibrationFailureReason : std::uint8_t {
  NONE,
  INVALID_CONFIG,
  STATIC_DELTA_TOO_LARGE,
  MEAN_OFFSET_TOO_LARGE,
  ACCEL_STATIC_DELTA_TOO_LARGE,
  ACCEL_NORM_OUT_OF_RANGE,
  GRAVITY_MEAN_OUT_OF_RANGE = ACCEL_NORM_OUT_OF_RANGE
};

struct StaticCalibrationConfig {
  std::size_t target_samples = 6000;
  float gyro_lsb_to_radps = 0.0f;
  float accel_lsb_to_g = 0.0f;
  float max_gyro_static_delta_radps = 0.15f;
  float max_accel_static_delta_g = 0.05f;
  float max_abs_gyro_mean_radps = 0.01f;
  float max_accel_norm_error_g = 0.05f;
};

struct StaticCalibrationStatus {
  StaticCalibrationState state = StaticCalibrationState::IDLE;
  StaticCalibrationFailureReason failure_reason =
      StaticCalibrationFailureReason::NONE;
  std::size_t sample_count = 0;
  std::array<float, 3> gyro_offset_radps = {0.0f, 0.0f, 0.0f};
  float accel_norm_g = 0.0f;
  float accel_scale = 1.0f;
  float temperature_when_calibrated_c = 0.0f;
  float max_gyro_static_delta_radps = 0.0f;
  float max_accel_static_delta_g = 0.0f;
  float max_abs_gyro_mean_radps = 0.0f;
  float quality_error = 0.0f;
};

class StaticCalibrationAccumulator {
 public:
  constexpr StaticCalibrationAccumulator() = default;

  constexpr explicit StaticCalibrationAccumulator(
      StaticCalibrationConfig config) {
    Start(config);
  }

  constexpr bool Start(StaticCalibrationConfig config) {
    config_ = config;
    ResetState();

    if (config_.target_samples == 0 || config_.gyro_lsb_to_radps <= 0.0f ||
        config_.accel_lsb_to_g <= 0.0f ||
        config_.max_gyro_static_delta_radps <= 0.0f ||
        config_.max_accel_static_delta_g <= 0.0f ||
        config_.max_abs_gyro_mean_radps <= 0.0f ||
        config_.max_accel_norm_error_g <= 0.0f) {
      status_.state = StaticCalibrationState::FAILED;
      status_.failure_reason = StaticCalibrationFailureReason::INVALID_CONFIG;
      return false;
    }

    status_.state = StaticCalibrationState::RUNNING;
    return true;
  }

  constexpr void Abort() { ResetState(); }

  constexpr bool IsRunning() const {
    return status_.state == StaticCalibrationState::RUNNING;
  }

  constexpr StaticCalibrationStatus GetStatus() const { return status_; }

  constexpr StaticCalibrationStatus AddRawSample(
      std::array<std::int16_t, 3> gyro_raw,
      std::array<std::int16_t, 3> accel_raw,
      float temperature_c) {
    if (!IsRunning()) {
      return status_;
    }

    AddGyroRawSample(gyro_raw);
    AddAccelRawSample(accel_raw);
    temperature_sum_c_ += temperature_c;
    ++status_.sample_count;
    UpdateGyroQualityError();
    UpdateAccelQualityError();

    if (status_.max_gyro_static_delta_radps >
        config_.max_gyro_static_delta_radps) {
      status_.state = StaticCalibrationState::FAILED;
      status_.failure_reason =
          StaticCalibrationFailureReason::STATIC_DELTA_TOO_LARGE;
      return status_;
    }

    if (status_.max_accel_static_delta_g >
        config_.max_accel_static_delta_g) {
      status_.state = StaticCalibrationState::FAILED;
      status_.failure_reason =
          StaticCalibrationFailureReason::ACCEL_STATIC_DELTA_TOO_LARGE;
      return status_;
    }

    if (status_.sample_count >= config_.target_samples) {
      UpdateMeanGyroOffset();
      UpdateAccelCalibration();
      status_.temperature_when_calibrated_c =
          temperature_sum_c_ / static_cast<float>(status_.sample_count);

      if (status_.max_abs_gyro_mean_radps >
          config_.max_abs_gyro_mean_radps) {
        status_.state = StaticCalibrationState::FAILED;
        status_.failure_reason =
            StaticCalibrationFailureReason::MEAN_OFFSET_TOO_LARGE;
        return status_;
      }
      if (Abs(status_.accel_norm_g - 1.0f) >
          config_.max_accel_norm_error_g) {
        status_.state = StaticCalibrationState::FAILED;
        status_.failure_reason =
            StaticCalibrationFailureReason::ACCEL_NORM_OUT_OF_RANGE;
        return status_;
      }
      status_.state = StaticCalibrationState::SUCCEEDED;
    }

    return status_;
  }

 private:
  static constexpr float Abs(float value) {
    return value >= 0.0f ? value : -value;
  }

  static constexpr float Sqrt(float value) {
    if (value <= 0.0f) {
      return 0.0f;
    }

    float estimate = value >= 1.0f ? value : 1.0f;
    for (std::size_t i = 0; i < 16; ++i) {
      estimate = 0.5f * (estimate + value / estimate);
    }
    return estimate;
  }

  constexpr void ResetState() {
    status_ = {};
    gyro_sum_ = {0, 0, 0};
    accel_sum_ = {0, 0, 0};
    gyro_min_raw_ = {0, 0, 0};
    gyro_max_raw_ = {0, 0, 0};
    accel_min_norm_g_ = 0.0f;
    accel_max_norm_g_ = 0.0f;
    temperature_sum_c_ = 0.0f;
    has_gyro_sample_ = false;
    has_accel_sample_ = false;
  }

  constexpr void AddGyroRawSample(std::array<std::int16_t, 3> raw) {
    UpdateRawRange(raw, gyro_min_raw_, gyro_max_raw_, has_gyro_sample_);
    for (std::size_t i = 0; i < raw.size(); ++i) {
      gyro_sum_[i] += raw[i];
    }
  }

  constexpr void AddAccelRawSample(std::array<std::int16_t, 3> raw) {
    const float accel_x_g = static_cast<float>(raw[0]) * config_.accel_lsb_to_g;
    const float accel_y_g = static_cast<float>(raw[1]) * config_.accel_lsb_to_g;
    const float accel_z_g = static_cast<float>(raw[2]) * config_.accel_lsb_to_g;
    const float accel_norm_g =
        Sqrt(accel_x_g * accel_x_g + accel_y_g * accel_y_g +
             accel_z_g * accel_z_g);
    if (!has_accel_sample_) {
      accel_min_norm_g_ = accel_norm_g;
      accel_max_norm_g_ = accel_norm_g;
      has_accel_sample_ = true;
    } else {
      if (accel_norm_g < accel_min_norm_g_) {
        accel_min_norm_g_ = accel_norm_g;
      }
      if (accel_norm_g > accel_max_norm_g_) {
        accel_max_norm_g_ = accel_norm_g;
      }
    }
    for (std::size_t i = 0; i < raw.size(); ++i) {
      accel_sum_[i] += raw[i];
    }
  }

  static constexpr void UpdateRawRange(
      std::array<std::int16_t, 3> raw,
      std::array<std::int16_t, 3>& min_raw,
      std::array<std::int16_t, 3>& max_raw,
      bool& has_sample) {
    if (!has_sample) {
      min_raw = raw;
      max_raw = raw;
      has_sample = true;
      return;
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] < min_raw[i]) {
        min_raw[i] = raw[i];
      }
      if (raw[i] > max_raw[i]) {
        max_raw[i] = raw[i];
      }
    }
  }

  constexpr void UpdateGyroQualityError() {
    std::int32_t max_delta = 0;
    for (std::size_t i = 0; i < gyro_min_raw_.size(); ++i) {
      const std::int32_t delta = static_cast<std::int32_t>(gyro_max_raw_[i]) -
                                 static_cast<std::int32_t>(gyro_min_raw_[i]);
      if (delta > max_delta) {
        max_delta = delta;
      }
    }
    status_.max_gyro_static_delta_radps =
        static_cast<float>(max_delta) * config_.gyro_lsb_to_radps;
    status_.quality_error = status_.max_gyro_static_delta_radps;
  }

  constexpr void UpdateAccelQualityError() {
    status_.max_accel_static_delta_g = accel_max_norm_g_ - accel_min_norm_g_;
  }

  constexpr void UpdateMeanGyroOffset() {
    const float sample_count = static_cast<float>(status_.sample_count);
    float max_abs_mean = 0.0f;
    for (std::size_t i = 0; i < status_.gyro_offset_radps.size(); ++i) {
      status_.gyro_offset_radps[i] =
          static_cast<float>(gyro_sum_[i]) / sample_count *
          config_.gyro_lsb_to_radps;
      const float abs_mean = Abs(status_.gyro_offset_radps[i]);
      if (abs_mean > max_abs_mean) {
        max_abs_mean = abs_mean;
      }
    }
    status_.max_abs_gyro_mean_radps = max_abs_mean;
  }

  constexpr void UpdateAccelCalibration() {
    const float sample_count = static_cast<float>(status_.sample_count);
    std::array<float, 3> mean_g = {0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < mean_g.size(); ++i) {
      mean_g[i] =
          static_cast<float>(accel_sum_[i]) / sample_count *
          config_.accel_lsb_to_g;
    }

    status_.accel_norm_g = Sqrt(mean_g[0] * mean_g[0] +
                                mean_g[1] * mean_g[1] +
                                mean_g[2] * mean_g[2]);
    status_.accel_scale =
        status_.accel_norm_g > 0.0f ? 1.0f / status_.accel_norm_g : 1.0f;
  }

  StaticCalibrationConfig config_;
  StaticCalibrationStatus status_;
  std::array<std::int64_t, 3> gyro_sum_ = {0, 0, 0};
  std::array<std::int64_t, 3> accel_sum_ = {0, 0, 0};
  std::array<std::int16_t, 3> gyro_min_raw_ = {0, 0, 0};
  std::array<std::int16_t, 3> gyro_max_raw_ = {0, 0, 0};
  float accel_min_norm_g_ = 0.0f;
  float accel_max_norm_g_ = 0.0f;
  float temperature_sum_c_ = 0.0f;
  bool has_gyro_sample_ = false;
  bool has_accel_sample_ = false;
};

enum class GyroCalibrationState : std::uint8_t {
  IDLE,
  RUNNING,
  SUCCEEDED,
  FAILED
};

enum class GyroCalibrationFailureReason : std::uint8_t {
  NONE,
  INVALID_CONFIG,
  STATIC_DELTA_TOO_LARGE,
  MEAN_OFFSET_TOO_LARGE,
  ACCEL_STATIC_DELTA_TOO_LARGE,
  GRAVITY_MEAN_OUT_OF_RANGE
};

struct GyroCalibrationConfig {
  std::size_t target_samples = 6000;
  float raw_lsb_to_radps = 0.0f;
  float raw_lsb_to_g = 0.0f;
  float max_static_delta_radps = 0.15f;
  float max_abs_mean_radps = 0.01f;
  float max_accel_static_delta_g = 0.05f;
  float max_abs_gravity_error_g = 0.05f;
};

struct GyroCalibrationStatus {
  GyroCalibrationState state = GyroCalibrationState::IDLE;
  GyroCalibrationFailureReason failure_reason =
      GyroCalibrationFailureReason::NONE;
  std::size_t sample_count = 0;
  std::array<float, 3> offset_radps = {0.0f, 0.0f, 0.0f};
  float max_static_delta_radps = 0.0f;
  float max_abs_mean_radps = 0.0f;
  float accel_norm_g = 0.0f;
  float accel_scale = 1.0f;
  float temperature_when_calibrated_c = 0.0f;
  float max_accel_static_delta_g = 0.0f;
  float quality_error = 0.0f;
};

class GyroCalibrationAccumulator {
 public:
  constexpr GyroCalibrationAccumulator() = default;

  constexpr explicit GyroCalibrationAccumulator(GyroCalibrationConfig config) {
    Start(config);
  }

  constexpr bool Start(GyroCalibrationConfig config) {
    config_ = config;
    status_ = {};
    gyro_sum_ = {0, 0, 0};
    gyro_min_raw_ = {0, 0, 0};
    gyro_max_raw_ = {0, 0, 0};
    has_sample_ = false;

    if (config_.target_samples == 0 || config_.raw_lsb_to_radps <= 0.0f ||
        config_.max_static_delta_radps <= 0.0f ||
        config_.max_abs_mean_radps <= 0.0f ||
        config_.max_accel_static_delta_g <= 0.0f ||
        config_.max_abs_gravity_error_g <= 0.0f) {
      status_.state = GyroCalibrationState::FAILED;
      status_.failure_reason = GyroCalibrationFailureReason::INVALID_CONFIG;
      return false;
    }

    status_.state = GyroCalibrationState::RUNNING;
    return true;
  }

  constexpr void Abort() {
    status_ = {};
    gyro_sum_ = {0, 0, 0};
    gyro_min_raw_ = {0, 0, 0};
    gyro_max_raw_ = {0, 0, 0};
    has_sample_ = false;
  }

  constexpr bool IsRunning() const {
    return status_.state == GyroCalibrationState::RUNNING;
  }

  constexpr GyroCalibrationStatus GetStatus() const { return status_; }

  constexpr GyroCalibrationStatus
  AddRawSample(std::array<std::int16_t, 3> gyro_raw) {
    if (!IsRunning()) {
      return status_;
    }

    UpdateRawRange(gyro_raw);
    for (std::size_t i = 0; i < gyro_raw.size(); ++i) {
      gyro_sum_[i] += gyro_raw[i];
    }
    ++status_.sample_count;
    UpdateQualityError();

    if (status_.max_static_delta_radps > config_.max_static_delta_radps) {
      status_.state = GyroCalibrationState::FAILED;
      status_.failure_reason =
          GyroCalibrationFailureReason::STATIC_DELTA_TOO_LARGE;
      return status_;
    }

    if (status_.sample_count >= config_.target_samples) {
      UpdateMeanOffset();
      if (status_.max_abs_mean_radps > config_.max_abs_mean_radps) {
        status_.state = GyroCalibrationState::FAILED;
        status_.failure_reason =
            GyroCalibrationFailureReason::MEAN_OFFSET_TOO_LARGE;
        return status_;
      }
      status_.state = GyroCalibrationState::SUCCEEDED;
    }

    return status_;
  }

 private:
  static constexpr float Abs(float value) {
    return value >= 0.0f ? value : -value;
  }

  constexpr void UpdateRawRange(std::array<std::int16_t, 3> raw) {
    if (!has_sample_) {
      gyro_min_raw_ = raw;
      gyro_max_raw_ = raw;
      has_sample_ = true;
      return;
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] < gyro_min_raw_[i]) {
        gyro_min_raw_[i] = raw[i];
      }
      if (raw[i] > gyro_max_raw_[i]) {
        gyro_max_raw_[i] = raw[i];
      }
    }
  }

  constexpr void UpdateQualityError() {
    std::int32_t max_delta = 0;
    for (std::size_t i = 0; i < gyro_min_raw_.size(); ++i) {
      const std::int32_t delta = static_cast<std::int32_t>(gyro_max_raw_[i]) -
                                 static_cast<std::int32_t>(gyro_min_raw_[i]);
      if (delta > max_delta) {
        max_delta = delta;
      }
    }
    status_.max_static_delta_radps =
        static_cast<float>(max_delta) * config_.raw_lsb_to_radps;
    status_.quality_error = status_.max_static_delta_radps;
  }

  constexpr void UpdateMeanOffset() {
    const float sample_count = static_cast<float>(status_.sample_count);
    float max_abs_mean = 0.0f;
    for (std::size_t i = 0; i < status_.offset_radps.size(); ++i) {
      status_.offset_radps[i] =
          static_cast<float>(gyro_sum_[i]) / sample_count *
          config_.raw_lsb_to_radps;
      const float abs_mean = Abs(status_.offset_radps[i]);
      if (abs_mean > max_abs_mean) {
        max_abs_mean = abs_mean;
      }
    }
    status_.max_abs_mean_radps = max_abs_mean;
  }

  GyroCalibrationConfig config_;
  GyroCalibrationStatus status_;
  std::array<std::int64_t, 3> gyro_sum_ = {0, 0, 0};
  std::array<std::int16_t, 3> gyro_min_raw_ = {0, 0, 0};
  std::array<std::int16_t, 3> gyro_max_raw_ = {0, 0, 0};
  bool has_sample_ = false;
};

} // namespace BMI088Calibration
