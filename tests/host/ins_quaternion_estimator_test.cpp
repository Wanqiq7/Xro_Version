#include <cassert>
#include <array>
#include <cmath>

#include "Modules/INS/QuaternionInsEstimator.hpp"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

bool Near(float actual, float expected, float tolerance) {
  return std::fabs(actual - expected) <= tolerance;
}

float QuaternionDot(const std::array<float, 4>& lhs,
                    const std::array<float, 4>& rhs) {
  return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2] +
         lhs[3] * rhs[3];
}

App::Vector3f GravityForAttitude(float pitch_deg, float roll_deg) {
  const float pitch = pitch_deg * kDegToRad;
  const float roll = roll_deg * kDegToRad;
  return {std::sin(roll) * std::cos(pitch), -std::sin(pitch),
          std::cos(roll) * std::cos(pitch)};
}

void InitializeEstimator(InsAlgorithm::QuaternionInsEstimator& estimator,
                         const App::Vector3f& accel_g) {
  for (int i = 0; i < 100; ++i) {
    estimator.Update({0.0f, 0.0f, 0.0f}, accel_g, 0.001f);
  }
}

void ExpectStaticInitAlignsPitchAndRollToGravity() {
  InsAlgorithm::QuaternionInsEstimator estimator;
  const App::Vector3f accel_g = GravityForAttitude(20.0f, -15.0f);

  InitializeEstimator(estimator, accel_g);
  const auto state = estimator.GetState();

  assert(Near(state.pitch_deg, 20.0f, 0.5f));
  assert(Near(state.roll_deg, -15.0f, 0.5f));
  assert(Near(state.yaw_deg, 0.0f, 0.01f));
  assert(Near(state.yaw_total_deg, 0.0f, 0.01f));
  assert(Near(state.quaternion[0], std::cos(20.0f * kDegToRad / 2.0f) *
                                      std::cos(-15.0f * kDegToRad / 2.0f),
              0.05f));
}

void ExpectYawWrapsSingleTurnAndKeepsMultiTurnTotal() {
  InsAlgorithm::QuaternionInsEstimator estimator;
  InitializeEstimator(estimator, {0.0f, 0.0f, 1.0f});

  constexpr float yaw_rate_radps = 90.0f * kDegToRad;
  for (int i = 0; i < 5000; ++i) {
    estimator.Update({0.0f, 0.0f, yaw_rate_radps}, {0.0f, 0.0f, 1.0f},
                     0.001f);
  }

  const auto state = estimator.GetState();
  assert(Near(state.yaw_total_deg, 450.0f, 2.0f));
  assert(Near(state.yaw_deg, 90.0f, 2.0f));
}

void ExpectStaticGyroBiasConvergesOnlyOnObservableAxes() {
  InsAlgorithm::QuaternionInsEstimator estimator;
  const App::Vector3f accel_g{0.0f, 0.0f, 1.0f};
  InitializeEstimator(estimator, accel_g);

  const App::Vector3f gyro_bias_radps{0.04f, -0.03f, 0.02f};
  for (int i = 0; i < 12000; ++i) {
    estimator.Update(gyro_bias_radps, accel_g, 0.001f);
  }

  const auto state = estimator.GetState();
  assert(Near(state.gyro_bias_radps.x, gyro_bias_radps.x, 0.015f));
  assert(Near(state.gyro_bias_radps.y, gyro_bias_radps.y, 0.015f));
  assert(Near(state.gyro_bias_radps.z, 0.0f, 0.0001f));
  assert(Near(state.corrected_gyro_radps.x, 0.0f, 0.015f));
  assert(Near(state.corrected_gyro_radps.y, 0.0f, 0.015f));
  assert(Near(state.corrected_gyro_radps.z, gyro_bias_radps.z, 0.0001f));
}

void ExpectBodyFrameGyroUsesQuaternionKinematics() {
  InsAlgorithm::QuaternionInsEstimator estimator;
  InitializeEstimator(estimator, {0.0f, 0.0f, 1.0f});

  constexpr float axis_rate_radps = 90.0f * kDegToRad;
  for (int i = 0; i < 1000; ++i) {
    estimator.Update({axis_rate_radps, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                     0.001f);
  }
  for (int i = 0; i < 1000; ++i) {
    estimator.Update({0.0f, axis_rate_radps, 0.0f}, {0.0f, 0.0f, 0.0f},
                     0.001f);
  }

  const auto state = estimator.GetState();
  const std::array<float, 4> expected{0.5f, 0.5f, 0.5f, 0.5f};
  assert(std::fabs(QuaternionDot(state.quaternion, expected)) > 0.999f);
}

}  // namespace

int main() {
  ExpectStaticInitAlignsPitchAndRollToGravity();
  ExpectYawWrapsSingleTurnAndKeepsMultiTurnTotal();
  ExpectStaticGyroBiasConvergesOnlyOnObservableAxes();
  ExpectBodyFrameGyroUsesQuaternionKinematics();
  return 0;
}
