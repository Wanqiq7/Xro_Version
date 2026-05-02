#include "../Modules/BMI088/BMI088Calibration.hpp"

#include <array>

constexpr float kDegToRad = 0.017453292519943295769f;
constexpr float kBmi088Gyro2000DpsLsbToRadps = kDegToRad / 16.384f;

constexpr bool Near(float left, float right, float tolerance) {
  return left > right ? (left - right) <= tolerance : (right - left) <= tolerance;
}

constexpr bool ComputesMeanOffsetInRadps() {
  BMI088Calibration::GyroCalibrationAccumulator accumulator(
      BMI088Calibration::GyroCalibrationConfig{.target_samples = 4,
                                               .raw_lsb_to_radps = 0.001f,
                                               .max_static_delta_radps = 0.020f,
                                               .max_abs_mean_radps = 0.250f});

  accumulator.AddRawSample({100, -200, 50});
  accumulator.AddRawSample({102, -198, 48});
  accumulator.AddRawSample({98, -202, 52});
  accumulator.AddRawSample({100, -200, 50});

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::GyroCalibrationState::SUCCEEDED &&
         status.sample_count == 4 && Near(status.offset_radps[0], 0.100f, 0.00001f) &&
         Near(status.offset_radps[1], -0.200f, 0.00001f) &&
         Near(status.offset_radps[2], 0.050f, 0.00001f) &&
         Near(status.max_static_delta_radps, 0.004f, 0.00001f) &&
         Near(status.max_abs_mean_radps, 0.200f, 0.00001f);
}

constexpr bool DoesNotCompleteBeforeEnoughSamples() {
  BMI088Calibration::GyroCalibrationAccumulator accumulator(
      BMI088Calibration::GyroCalibrationConfig{.target_samples = 3,
                                               .raw_lsb_to_radps = 0.002f,
                                               .max_static_delta_radps = 0.020f,
                                               .max_abs_mean_radps = 0.100f});

  accumulator.AddRawSample({1, 2, 3});
  accumulator.AddRawSample({2, 3, 4});

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::GyroCalibrationState::RUNNING &&
         status.sample_count == 2 &&
         Near(status.offset_radps[0], 0.0f, 0.0f);
}

constexpr bool FailsWhenStaticQualityThresholdExceeded() {
  BMI088Calibration::GyroCalibrationAccumulator accumulator(
      BMI088Calibration::GyroCalibrationConfig{.target_samples = 4,
                                               .raw_lsb_to_radps = 0.01f,
                                               .max_static_delta_radps = 0.05f,
                                               .max_abs_mean_radps = 0.20f});

  accumulator.AddRawSample({10, 10, 10});
  accumulator.AddRawSample({11, 12, 13});
  accumulator.AddRawSample({20, 10, 10});

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::GyroCalibrationState::FAILED &&
         status.failure_reason ==
             BMI088Calibration::GyroCalibrationFailureReason::
                 STATIC_DELTA_TOO_LARGE &&
         status.sample_count == 3 &&
         Near(status.max_static_delta_radps, 0.10f, 0.00001f);
}

constexpr bool FailsWhenMeanOffsetThresholdExceeded() {
  BMI088Calibration::GyroCalibrationAccumulator accumulator(
      BMI088Calibration::GyroCalibrationConfig{.target_samples = 3,
                                               .raw_lsb_to_radps = 0.001f,
                                               .max_static_delta_radps = 0.020f,
                                               .max_abs_mean_radps = 0.010f});

  accumulator.AddRawSample({20, 1, 1});
  accumulator.AddRawSample({21, 1, 1});
  accumulator.AddRawSample({19, 1, 1});

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::GyroCalibrationState::FAILED &&
         status.failure_reason ==
             BMI088Calibration::GyroCalibrationFailureReason::
                 MEAN_OFFSET_TOO_LARGE &&
         Near(status.offset_radps[0], 0.020f, 0.00001f) &&
         Near(status.max_abs_mean_radps, 0.020f, 0.00001f);
}

constexpr bool ComputesCompleteStaticCalibration() {
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 4,
          .gyro_lsb_to_radps = 0.001f,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.030f,
          .max_abs_gyro_mean_radps = 0.250f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({100, -200, 50}, {0, 0, 1000}, 42.0f);
  accumulator.AddRawSample({102, -198, 48}, {1, -1, 1001}, 42.4f);
  accumulator.AddRawSample({98, -202, 52}, {-1, 1, 999}, 41.6f);
  accumulator.AddRawSample({100, -200, 50}, {0, 0, 1000}, 42.0f);

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::StaticCalibrationState::SUCCEEDED &&
         status.failure_reason ==
             BMI088Calibration::StaticCalibrationFailureReason::NONE &&
         status.sample_count == 4 &&
         Near(status.gyro_offset_radps[0], 0.100f, 0.00001f) &&
         Near(status.gyro_offset_radps[1], -0.200f, 0.00001f) &&
         Near(status.gyro_offset_radps[2], 0.050f, 0.00001f) &&
         Near(status.accel_norm_g, 1.000f, 0.00001f) &&
         Near(status.accel_scale, 1.000f, 0.00001f) &&
         Near(status.temperature_when_calibrated_c, 42.0f, 0.00001f);
}

constexpr bool StartupStaticCalibrationCompletesBelowHeatTarget() {
  const float heating_target_temperature_c = 45.0f;
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 4,
          .gyro_lsb_to_radps = 0.001f,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.030f,
          .max_abs_gyro_mean_radps = 0.250f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 31.5f);
  accumulator.AddRawSample({17, -7, 5}, {1, 0, 1000}, 31.7f);
  accumulator.AddRawSample({15, -9, 3}, {-1, 0, 1000}, 31.9f);
  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 32.1f);

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::StaticCalibrationState::SUCCEEDED &&
         status.sample_count == 4 &&
         status.temperature_when_calibrated_c <
             heating_target_temperature_c - 2.0f &&
         Near(status.gyro_offset_radps[0], 0.016f, 0.00001f) &&
         Near(status.gyro_offset_radps[1], -0.008f, 0.00001f) &&
         Near(status.gyro_offset_radps[2], 0.004f, 0.00001f) &&
         Near(status.temperature_when_calibrated_c, 31.8f, 0.00001f) &&
         Near(status.accel_norm_g, 1.000f, 0.00001f) &&
         Near(status.accel_scale, 1.000f, 0.00001f);
}

constexpr bool ConvertsHeroCodeBmi088GyroScaleToRadpsOffset() {
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 4,
          .gyro_lsb_to_radps = kBmi088Gyro2000DpsLsbToRadps,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.030f,
          .max_abs_gyro_mean_radps = 0.050f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 40.0f);
  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 40.0f);
  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 40.0f);
  accumulator.AddRawSample({16, -8, 4}, {0, 0, 1000}, 40.0f);

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::StaticCalibrationState::SUCCEEDED &&
         Near(kBmi088Gyro2000DpsLsbToRadps, 0.0010652644f, 0.00000001f) &&
         Near(status.gyro_offset_radps[0], 0.01704423f, 0.0000001f) &&
         Near(status.gyro_offset_radps[1], -0.008522115f, 0.0000001f) &&
         Near(status.gyro_offset_radps[2], 0.004261057f, 0.0000001f) &&
         Near(status.max_abs_gyro_mean_radps, 0.01704423f, 0.0000001f);
}

constexpr bool FailsWhenAccelStaticDeltaThresholdExceeded() {
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 4,
          .gyro_lsb_to_radps = 0.001f,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.010f,
          .max_abs_gyro_mean_radps = 0.250f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({0, 0, 0}, {0, 0, 1000}, 40.0f);
  accumulator.AddRawSample({1, -1, 0}, {0, 0, 1005}, 40.0f);
  accumulator.AddRawSample({0, 0, 1}, {0, 0, 1020}, 40.0f);

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::StaticCalibrationState::FAILED &&
         status.failure_reason ==
             BMI088Calibration::StaticCalibrationFailureReason::
                 ACCEL_STATIC_DELTA_TOO_LARGE &&
         status.sample_count == 3 &&
         Near(status.max_accel_static_delta_g, 0.020f, 0.00001f);
}

constexpr bool FailsWhenAverageGravityNormThresholdExceeded() {
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 3,
          .gyro_lsb_to_radps = 0.001f,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.030f,
          .max_abs_gyro_mean_radps = 0.250f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({0, 0, 0}, {0, 0, 1200}, 40.0f);
  accumulator.AddRawSample({1, -1, 0}, {0, 0, 1201}, 40.0f);
  accumulator.AddRawSample({0, 0, 1}, {0, 0, 1199}, 40.0f);

  const auto status = accumulator.GetStatus();
  return status.state == BMI088Calibration::StaticCalibrationState::FAILED &&
         status.failure_reason ==
             BMI088Calibration::StaticCalibrationFailureReason::
                 ACCEL_NORM_OUT_OF_RANGE &&
         Near(status.accel_norm_g, 1.200f, 0.00001f) &&
         Near(status.accel_scale, 1.0f / 1.2f, 0.00001f);
}

constexpr bool AbortAndRestartClearsStaticCalibrationState() {
  BMI088Calibration::StaticCalibrationAccumulator accumulator(
      BMI088Calibration::StaticCalibrationConfig{
          .target_samples = 3,
          .gyro_lsb_to_radps = 0.001f,
          .accel_lsb_to_g = 0.001f,
          .max_gyro_static_delta_radps = 0.020f,
          .max_accel_static_delta_g = 0.030f,
          .max_abs_gyro_mean_radps = 0.250f,
          .max_accel_norm_error_g = 0.050f});

  accumulator.AddRawSample({10, 20, -30}, {0, 0, 1000}, 35.0f);
  accumulator.AddRawSample({11, 19, -29}, {0, 1, 1000}, 35.5f);
  accumulator.Abort();

  const auto aborted_status = accumulator.GetStatus();
  if (aborted_status.state != BMI088Calibration::StaticCalibrationState::IDLE ||
      aborted_status.sample_count != 0 ||
      !Near(aborted_status.gyro_offset_radps[0], 0.0f, 0.0f) ||
      !Near(aborted_status.accel_norm_g, 0.0f, 0.0f) ||
      !Near(aborted_status.temperature_when_calibrated_c, 0.0f, 0.0f)) {
    return false;
  }

  accumulator.Start(BMI088Calibration::StaticCalibrationConfig{
      .target_samples = 3,
      .gyro_lsb_to_radps = 0.002f,
      .accel_lsb_to_g = 0.001f,
      .max_gyro_static_delta_radps = 0.030f,
      .max_accel_static_delta_g = 0.030f,
      .max_abs_gyro_mean_radps = 0.250f,
      .max_accel_norm_error_g = 0.050f});
  accumulator.AddRawSample({10, 0, 0}, {0, 0, 1000}, 36.0f);
  accumulator.AddRawSample({10, 0, 0}, {0, 0, 1000}, 37.0f);
  accumulator.AddRawSample({10, 0, 0}, {0, 0, 1000}, 38.0f);

  const auto restarted_status = accumulator.GetStatus();
  return restarted_status.state ==
             BMI088Calibration::StaticCalibrationState::SUCCEEDED &&
         restarted_status.sample_count == 3 &&
         Near(restarted_status.gyro_offset_radps[0], 0.020f, 0.00001f) &&
         Near(restarted_status.gyro_offset_radps[1], 0.0f, 0.0f) &&
         Near(restarted_status.gyro_offset_radps[2], 0.0f, 0.0f) &&
         Near(restarted_status.accel_norm_g, 1.000f, 0.00001f) &&
         Near(restarted_status.accel_scale, 1.000f, 0.00001f) &&
         Near(restarted_status.temperature_when_calibrated_c, 37.0f,
              0.00001f);
}

static_assert(ComputesMeanOffsetInRadps());
static_assert(DoesNotCompleteBeforeEnoughSamples());
static_assert(FailsWhenStaticQualityThresholdExceeded());
static_assert(FailsWhenMeanOffsetThresholdExceeded());
static_assert(ComputesCompleteStaticCalibration());
static_assert(StartupStaticCalibrationCompletesBelowHeatTarget());
static_assert(ConvertsHeroCodeBmi088GyroScaleToRadpsOffset());
static_assert(FailsWhenAccelStaticDeltaThresholdExceeded());
static_assert(FailsWhenAverageGravityNormThresholdExceeded());
static_assert(AbortAndRestartClearsStaticCalibrationState());

int main() { return 0; }
