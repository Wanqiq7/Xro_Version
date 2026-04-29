#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../../App/Topics/InsState.hpp"

namespace InsAlgorithm {

class QuaternionInsEstimator {
 public:
  struct Config {
    float attitude_process_noise = 10.0f;
    float bias_process_noise = 0.001f;
    float accel_measure_noise = 1000000.0f;
    float fading_factor = 1.0f;
    float accel_lpf_coef = 0.0f;
    float motion_accel_lpf_coef = 0.0085f;
    std::array<float, 3> gyro_scale{1.0f, 1.0f, 1.0f};
    float install_yaw_offset_deg = 0.0f;
    float install_pitch_offset_deg = 0.0f;
    float install_roll_offset_deg = 0.0f;
  };

  struct State {
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;
    float roll_deg = 0.0f;
    float yaw_total_deg = 0.0f;
    std::array<float, 4> quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    App::Vector3f gyro_radps{};
    App::Vector3f gyro_bias_radps{};
    App::Vector3f motion_accel_body_g{};
    App::Vector3f motion_accel_nav_g{};
    App::Vector3f motion_accel_world_g{};
  };

  QuaternionInsEstimator() { Reset(); }

  explicit QuaternionInsEstimator(const Config& config) : config_(config) {
    Reset();
  }

  void Reset() {
    state_ = {};
    xhat_ = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    xhatminus_ = xhat_;
    f_ = Identity6();
    p_ = {};
    p_[0][0] = 100000.0f;
    p_[1][1] = 100000.0f;
    p_[2][2] = 100000.0f;
    p_[3][3] = 100000.0f;
    p_[4][4] = 100.0f;
    p_[5][5] = 100.0f;
    filtered_accel_mps2_ = {0.0f, 0.0f, kGravityMps2};
    init_accel_sum_g_ = {};
    init_accel_count_ = 0;
    motion_accel_body_lpf_g_ = {};
    yaw_round_count_ = 0;
    yaw_angle_last_deg_ = 0.0f;
    last_dt_s_ = 0.0f;
    update_count_ = 0;
    error_count_ = 0;
    converge_flag_ = false;
    initialized_ = false;
    install_param_dirty_ = true;
  }

  void Update(const App::Vector3f& gyro_radps, const App::Vector3f& accel_g,
              float dt_s) {
    const float safe_dt_s = SanitizeDt(dt_s);
    App::Vector3f corrected_gyro = gyro_radps;
    App::Vector3f corrected_accel = accel_g;
    CorrectInstallation(corrected_gyro, corrected_accel);
    last_dt_s_ = safe_dt_s;

    if (!initialized_) {
      if (!IsValidAccel(corrected_accel)) {
        return;
      }
      AccumulateInitialAccel(corrected_accel);
      if (init_accel_count_ < kInitialAccelSampleCount) {
        UpdateStateVectors(corrected_accel, false);
        return;
      }
      InitializeFromAccel(AverageInitialAccel());
      UpdateStateVectors(corrected_accel, false);
      return;
    }

    if (!IsValidAccel(corrected_accel)) {
      PredictQuaternionOnly(corrected_gyro, safe_dt_s);
      UpdateStateVectors(corrected_accel, true);
      return;
    }

    UpdateEkf(corrected_gyro, ToMps2(corrected_accel), safe_dt_s);
    UpdateStateVectors(corrected_accel, true);
  }

  const State& GetState() const { return state_; }

  bool IsInitialized() const { return initialized_; }

 private:
  using Vector3 = std::array<float, 3>;
  using Vector6 = std::array<float, 6>;
  using Matrix3 = std::array<std::array<float, 3>, 3>;
  using Matrix36 = std::array<std::array<float, 6>, 3>;
  using Matrix63 = std::array<std::array<float, 3>, 6>;
  using Matrix6 = std::array<std::array<float, 6>, 6>;

  struct AccelAttitude {
    float pitch_rad = 0.0f;
    float roll_rad = 0.0f;
  };

  static constexpr float kPi = 3.14159265358979323846f;
  static constexpr float kRadToDeg = 180.0f / kPi;
  static constexpr float kDegToRad = kPi / 180.0f;
  static constexpr float kGravityMps2 = 9.81f;
  static constexpr float kMaxDtS = 0.05f;
  static constexpr float kChiSquareThreshold = 1e-8f;
  static constexpr float kGyroStableThresholdRadps = 0.3f;
  static constexpr float kAccelStableToleranceMps2 = 0.5f;
  static constexpr float kMinMatrixPivot = 1e-12f;
  static constexpr int kInitialAccelSampleCount = 100;

  static Matrix6 Identity6() {
    Matrix6 matrix{};
    for (int i = 0; i < 6; ++i) {
      matrix[i][i] = 1.0f;
    }
    return matrix;
  }

  static float Clamp(float value, float min_value, float max_value) {
    if (value < min_value) {
      return min_value;
    }
    if (value > max_value) {
      return max_value;
    }
    return value;
  }

  static float SanitizeDt(float dt_s) {
    if (!(dt_s > 0.0f) || !std::isfinite(dt_s)) {
      return 0.0f;
    }
    return dt_s > kMaxDtS ? kMaxDtS : dt_s;
  }

  static float Norm(const App::Vector3f& vector) {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y +
                     vector.z * vector.z);
  }

  static float Norm(const Vector3& vector) {
    return std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] +
                     vector[2] * vector[2]);
  }

  static bool IsValidAccel(const App::Vector3f& accel_g) {
    return Norm(accel_g) > 1e-5f;
  }

  static Vector3 ToMps2(const App::Vector3f& accel_g) {
    return {accel_g.x * kGravityMps2, accel_g.y * kGravityMps2,
            accel_g.z * kGravityMps2};
  }

  static Vector3 Normalize(const Vector3& vector) {
    const float norm = Norm(vector);
    if (!(norm > 1e-8f)) {
      return {0.0f, 0.0f, 1.0f};
    }
    return {vector[0] / norm, vector[1] / norm, vector[2] / norm};
  }

  static float Dot(const App::Vector3f& lhs, const App::Vector3f& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  static App::Vector3f Cross(const App::Vector3f& lhs,
                             const App::Vector3f& rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
  }

  static App::Vector3f Normalize(const App::Vector3f& vector) {
    const float norm = Norm(vector);
    if (!(norm > 1e-8f)) {
      return {0.0f, 0.0f, 1.0f};
    }
    return {vector.x / norm, vector.y / norm, vector.z / norm};
  }

  static AccelAttitude AttitudeFromAccel(const App::Vector3f& accel_g) {
    const float horizontal_norm =
        std::sqrt(accel_g.x * accel_g.x + accel_g.z * accel_g.z);
    AccelAttitude attitude;
    attitude.pitch_rad = std::atan2(-accel_g.y, horizontal_norm);
    attitude.roll_rad = std::atan2(accel_g.x, accel_g.z);
    return attitude;
  }

  static std::array<float, 4> QuaternionFromEuler(float yaw_rad,
                                                  float pitch_rad,
                                                  float roll_rad) {
    const float half_yaw = yaw_rad * 0.5f;
    const float half_pitch = pitch_rad * 0.5f;
    const float half_roll = roll_rad * 0.5f;
    const float cy = std::cos(half_yaw);
    const float sy = std::sin(half_yaw);
    const float cp = std::cos(half_pitch);
    const float sp = std::sin(half_pitch);
    const float cr = std::cos(half_roll);
    const float sr = std::sin(half_roll);

    return {cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy};
  }

  static std::array<float, 4> QuaternionFromAccelAxisAngle(
      const App::Vector3f& accel_g) {
    const App::Vector3f acc_norm = Normalize(accel_g);
    const App::Vector3f gravity_norm{0.0f, 0.0f, 1.0f};
    const float dot = Clamp(Dot(acc_norm, gravity_norm), -1.0f, 1.0f);
    const float angle = std::acos(dot);
    App::Vector3f axis = Cross(acc_norm, gravity_norm);
    if (Norm(axis) <= 1e-6f) {
      axis = {1.0f, 0.0f, 0.0f};
    } else {
      axis = Normalize(axis);
    }

    const float half_angle = angle * 0.5f;
    const float sin_half_angle = std::sin(half_angle);
    return {std::cos(half_angle), axis.x * sin_half_angle,
            axis.y * sin_half_angle, axis.z * sin_half_angle};
  }

  static void NormalizeQuaternion(Vector6& state) {
    const float norm =
        std::sqrt(state[0] * state[0] + state[1] * state[1] +
                  state[2] * state[2] + state[3] * state[3]);
    if (!(norm > 1e-8f)) {
      state[0] = 1.0f;
      state[1] = 0.0f;
      state[2] = 0.0f;
      state[3] = 0.0f;
      return;
    }
    for (int i = 0; i < 4; ++i) {
      state[i] /= norm;
    }
  }

  static App::Vector3f GravityInBody(const Vector6& state) {
    const float q0 = state[0];
    const float q1 = state[1];
    const float q2 = state[2];
    const float q3 = state[3];
    return {2.0f * (q1 * q3 - q0 * q2),
            2.0f * (q0 * q1 + q2 * q3),
            q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3};
  }

  static App::Vector3f BodyToNav(const App::Vector3f& body,
                                 const Vector6& state) {
    const float q0 = state[0];
    const float q1 = state[1];
    const float q2 = state[2];
    const float q3 = state[3];

    return {
        2.0f * ((0.5f - q2 * q2 - q3 * q3) * body.x +
                (q1 * q2 - q0 * q3) * body.y +
                (q1 * q3 + q0 * q2) * body.z),
        2.0f * ((q1 * q2 + q0 * q3) * body.x +
                (0.5f - q1 * q1 - q3 * q3) * body.y +
                (q2 * q3 - q0 * q1) * body.z),
        2.0f * ((q1 * q3 - q0 * q2) * body.x +
                (q2 * q3 + q0 * q1) * body.y +
                (0.5f - q1 * q1 - q2 * q2) * body.z),
    };
  }

  static bool Invert3x3(const Matrix3& input, Matrix3& inverse) {
    const float a = input[0][0];
    const float b = input[0][1];
    const float c = input[0][2];
    const float d = input[1][0];
    const float e = input[1][1];
    const float f = input[1][2];
    const float g = input[2][0];
    const float h = input[2][1];
    const float i = input[2][2];

    const float cofactor00 = e * i - f * h;
    const float cofactor01 = c * h - b * i;
    const float cofactor02 = b * f - c * e;
    const float cofactor10 = f * g - d * i;
    const float cofactor11 = a * i - c * g;
    const float cofactor12 = c * d - a * f;
    const float cofactor20 = d * h - e * g;
    const float cofactor21 = b * g - a * h;
    const float cofactor22 = a * e - b * d;
    const float determinant =
        a * cofactor00 + b * cofactor10 + c * cofactor20;
    if (std::fabs(determinant) < kMinMatrixPivot) {
      return false;
    }

    const float inv_det = 1.0f / determinant;
    inverse[0][0] = cofactor00 * inv_det;
    inverse[0][1] = cofactor01 * inv_det;
    inverse[0][2] = cofactor02 * inv_det;
    inverse[1][0] = cofactor10 * inv_det;
    inverse[1][1] = cofactor11 * inv_det;
    inverse[1][2] = cofactor12 * inv_det;
    inverse[2][0] = cofactor20 * inv_det;
    inverse[2][1] = cofactor21 * inv_det;
    inverse[2][2] = cofactor22 * inv_det;
    return true;
  }

  void InitializeFromAccel(const App::Vector3f& accel_g) {
    const auto quaternion = QuaternionFromAccelAxisAngle(accel_g);
    for (int i = 0; i < 4; ++i) {
      xhat_[i] = quaternion[i];
    }
    xhat_[4] = 0.0f;
    xhat_[5] = 0.0f;
    NormalizeQuaternion(xhat_);
    initialized_ = true;
  }

  void AccumulateInitialAccel(const App::Vector3f& accel_g) {
    if (init_accel_count_ >= kInitialAccelSampleCount) {
      return;
    }
    init_accel_sum_g_.x += accel_g.x;
    init_accel_sum_g_.y += accel_g.y;
    init_accel_sum_g_.z += accel_g.z;
    ++init_accel_count_;
  }

  App::Vector3f AverageInitialAccel() const {
    if (init_accel_count_ == 0) {
      return {0.0f, 0.0f, 1.0f};
    }
    const float inv_count = 1.0f / static_cast<float>(init_accel_count_);
    return {init_accel_sum_g_.x * inv_count, init_accel_sum_g_.y * inv_count,
            init_accel_sum_g_.z * inv_count};
  }

  void RefreshInstallMatrixIfNeeded() {
    if (!install_param_dirty_ &&
        std::fabs(config_.install_yaw_offset_deg - last_install_yaw_deg_) <=
            0.001f &&
        std::fabs(config_.install_pitch_offset_deg - last_install_pitch_deg_) <=
            0.001f &&
        std::fabs(config_.install_roll_offset_deg - last_install_roll_deg_) <=
            0.001f) {
      return;
    }

    const float yaw = config_.install_yaw_offset_deg * kDegToRad;
    const float pitch = config_.install_pitch_offset_deg * kDegToRad;
    const float roll = config_.install_roll_offset_deg * kDegToRad;
    const float cos_yaw = std::cos(yaw);
    const float sin_yaw = std::sin(yaw);
    const float cos_pitch = std::cos(pitch);
    const float sin_pitch = std::sin(pitch);
    const float cos_roll = std::cos(roll);
    const float sin_roll = std::sin(roll);

    install_matrix_[0][0] = cos_yaw * cos_roll + sin_yaw * sin_pitch * sin_roll;
    install_matrix_[0][1] = cos_pitch * sin_yaw;
    install_matrix_[0][2] = cos_yaw * sin_roll -
                            cos_roll * sin_yaw * sin_pitch;
    install_matrix_[1][0] = cos_yaw * sin_pitch * sin_roll -
                            cos_roll * sin_yaw;
    install_matrix_[1][1] = cos_yaw * cos_pitch;
    install_matrix_[1][2] = -sin_yaw * sin_roll -
                            cos_yaw * cos_roll * sin_pitch;
    install_matrix_[2][0] = -cos_pitch * sin_roll;
    install_matrix_[2][1] = sin_pitch;
    install_matrix_[2][2] = cos_pitch * cos_roll;

    last_install_yaw_deg_ = config_.install_yaw_offset_deg;
    last_install_pitch_deg_ = config_.install_pitch_offset_deg;
    last_install_roll_deg_ = config_.install_roll_offset_deg;
    install_param_dirty_ = false;
  }

  void CorrectInstallation(App::Vector3f& gyro_radps, App::Vector3f& accel_g) {
    RefreshInstallMatrixIfNeeded();
    const App::Vector3f scaled_gyro{
        gyro_radps.x * config_.gyro_scale[0],
        gyro_radps.y * config_.gyro_scale[1],
        gyro_radps.z * config_.gyro_scale[2],
    };
    const App::Vector3f accel_temp = accel_g;

    gyro_radps = {
        install_matrix_[0][0] * scaled_gyro.x +
            install_matrix_[0][1] * scaled_gyro.y +
            install_matrix_[0][2] * scaled_gyro.z,
        install_matrix_[1][0] * scaled_gyro.x +
            install_matrix_[1][1] * scaled_gyro.y +
            install_matrix_[1][2] * scaled_gyro.z,
        install_matrix_[2][0] * scaled_gyro.x +
            install_matrix_[2][1] * scaled_gyro.y +
            install_matrix_[2][2] * scaled_gyro.z,
    };
    accel_g = {
        install_matrix_[0][0] * accel_temp.x +
            install_matrix_[0][1] * accel_temp.y +
            install_matrix_[0][2] * accel_temp.z,
        install_matrix_[1][0] * accel_temp.x +
            install_matrix_[1][1] * accel_temp.y +
            install_matrix_[1][2] * accel_temp.z,
        install_matrix_[2][0] * accel_temp.x +
            install_matrix_[2][1] * accel_temp.y +
            install_matrix_[2][2] * accel_temp.z,
    };
  }

  void SetTransitionMatrix(const App::Vector3f& gyro_radps, float dt_s) {
    f_ = Identity6();
    const float half_gx_dt = 0.5f * (gyro_radps.x - xhat_[4]) * dt_s;
    const float half_gy_dt = 0.5f * (gyro_radps.y - xhat_[5]) * dt_s;
    const float half_gz_dt = 0.5f * gyro_radps.z * dt_s;

    f_[0][1] = -half_gx_dt;
    f_[0][2] = -half_gy_dt;
    f_[0][3] = -half_gz_dt;
    f_[1][0] = half_gx_dt;
    f_[1][2] = half_gz_dt;
    f_[1][3] = -half_gy_dt;
    f_[2][0] = half_gy_dt;
    f_[2][1] = -half_gz_dt;
    f_[2][3] = half_gx_dt;
    f_[3][0] = half_gz_dt;
    f_[3][1] = half_gy_dt;
    f_[3][2] = -half_gx_dt;
  }

  void SetBiasLinearization(float dt_s) {
    const float q0 = xhatminus_[0];
    const float q1 = xhatminus_[1];
    const float q2 = xhatminus_[2];
    const float q3 = xhatminus_[3];

    f_[0][4] = q1 * dt_s * 0.5f;
    f_[0][5] = q2 * dt_s * 0.5f;
    f_[1][4] = -q0 * dt_s * 0.5f;
    f_[1][5] = q3 * dt_s * 0.5f;
    f_[2][4] = -q3 * dt_s * 0.5f;
    f_[2][5] = -q0 * dt_s * 0.5f;
    f_[3][4] = q2 * dt_s * 0.5f;
    f_[3][5] = -q1 * dt_s * 0.5f;
  }

  void PredictState() {
    Vector6 next{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        next[row] += f_[row][col] * xhat_[col];
      }
    }
    xhatminus_ = next;
    NormalizeQuaternion(xhatminus_);
  }

  void PredictCovariance(float dt_s) {
    Matrix6 fp{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        for (int k = 0; k < 6; ++k) {
          fp[row][col] += f_[row][k] * p_[k][col];
        }
      }
    }

    Matrix6 next{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        for (int k = 0; k < 6; ++k) {
          next[row][col] += fp[row][k] * f_[col][k];
        }
      }
    }

    const float lambda = Clamp(config_.fading_factor, 1e-6f, 1.0f);
    next[4][4] = std::min(next[4][4] / lambda, 10000.0f);
    next[5][5] = std::min(next[5][5] / lambda, 10000.0f);
    for (int i = 0; i < 4; ++i) {
      next[i][i] += config_.attitude_process_noise * dt_s;
    }
    next[4][4] += config_.bias_process_noise * dt_s;
    next[5][5] += config_.bias_process_noise * dt_s;
    pminus_ = next;
  }

  Matrix36 BuildObservationMatrix() const {
    const float q0 = xhatminus_[0];
    const float q1 = xhatminus_[1];
    const float q2 = xhatminus_[2];
    const float q3 = xhatminus_[3];
    Matrix36 h{};
    h[0][0] = -2.0f * q2;
    h[0][1] = 2.0f * q3;
    h[0][2] = -2.0f * q0;
    h[0][3] = 2.0f * q1;
    h[1][0] = 2.0f * q1;
    h[1][1] = 2.0f * q0;
    h[1][2] = 2.0f * q3;
    h[1][3] = 2.0f * q2;
    h[2][0] = 2.0f * q0;
    h[2][1] = -2.0f * q1;
    h[2][2] = -2.0f * q2;
    h[2][3] = 2.0f * q3;
    return h;
  }

  Vector3 PredictedGravityMeasurement() const {
    const float q0 = xhatminus_[0];
    const float q1 = xhatminus_[1];
    const float q2 = xhatminus_[2];
    const float q3 = xhatminus_[3];
    return {2.0f * (q1 * q3 - q0 * q2),
            2.0f * (q0 * q1 + q2 * q3),
            q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3};
  }

  static Matrix3 BuildInnovationCovariance(const Matrix36& h,
                                           const Matrix6& pminus,
                                           float measurement_noise) {
    Matrix3 innovation{};
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        for (int i = 0; i < 6; ++i) {
          for (int j = 0; j < 6; ++j) {
            innovation[row][col] += h[row][i] * pminus[i][j] * h[col][j];
          }
        }
      }
      innovation[row][row] += measurement_noise;
    }
    return innovation;
  }

  static Matrix63 BuildKalmanGain(const Matrix36& h, const Matrix6& pminus,
                                  const Matrix3& innovation_inverse) {
    Matrix63 gain{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 3; ++col) {
        for (int h_row = 0; h_row < 3; ++h_row) {
          for (int p_col = 0; p_col < 6; ++p_col) {
            gain[row][col] += pminus[row][p_col] * h[h_row][p_col] *
                              innovation_inverse[h_row][col];
          }
        }
      }
    }
    return gain;
  }

  static Matrix6 UpdateCovariance(const Matrix63& gain, const Matrix36& h,
                                  const Matrix6& pminus) {
    Matrix6 kh{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        for (int k = 0; k < 3; ++k) {
          kh[row][col] += gain[row][k] * h[k][col];
        }
      }
    }

    Matrix6 khp{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        for (int k = 0; k < 6; ++k) {
          khp[row][col] += kh[row][k] * pminus[k][col];
        }
      }
    }

    Matrix6 updated{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 6; ++col) {
        updated[row][col] = pminus[row][col] - khp[row][col];
      }
    }
    return updated;
  }

  bool ApplyMeasurementUpdate(const Vector3& measured_accel_direction,
                              bool stable_flag, float dt_s) {
    const Matrix36 h = BuildObservationMatrix();
    Matrix3 innovation = BuildInnovationCovariance(
        h, pminus_, std::max(config_.accel_measure_noise, 1e-6f));
    Matrix3 innovation_inverse{};
    if (!Invert3x3(innovation, innovation_inverse)) {
      xhat_ = xhatminus_;
      p_ = pminus_;
      return false;
    }

    const Vector3 predicted = PredictedGravityMeasurement();
    Vector3 residual{};
    for (int i = 0; i < 3; ++i) {
      residual[i] = measured_accel_direction[i] - predicted[i];
      orientation_cosine_[i] =
          std::acos(Clamp(std::fabs(predicted[i]), 0.0f, 1.0f));
    }

    Vector3 weighted_residual{};
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        weighted_residual[row] += innovation_inverse[row][col] * residual[col];
      }
    }
    float chi_square = 0.0f;
    for (int i = 0; i < 3; ++i) {
      chi_square += residual[i] * weighted_residual[i];
    }

    if (chi_square < 0.5f * kChiSquareThreshold) {
      converge_flag_ = true;
    }

    if (chi_square > kChiSquareThreshold && converge_flag_) {
      if (stable_flag) {
        ++error_count_;
      } else {
        error_count_ = 0;
      }
      if (error_count_ <= 50) {
        xhat_ = xhatminus_;
        p_ = pminus_;
        return true;
      }
      converge_flag_ = false;
    } else {
      error_count_ = 0;
    }

    float adaptive_gain_scale = 1.0f;
    if (chi_square > 0.1f * kChiSquareThreshold && converge_flag_) {
      adaptive_gain_scale =
          (kChiSquareThreshold - chi_square) / (0.9f * kChiSquareThreshold);
      adaptive_gain_scale = Clamp(adaptive_gain_scale, 0.0f, 1.0f);
    }

    Matrix63 gain = BuildKalmanGain(h, pminus_, innovation_inverse);
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 3; ++col) {
        gain[row][col] *= adaptive_gain_scale;
      }
    }
    for (int row = 4; row < 6; ++row) {
      for (int col = 0; col < 3; ++col) {
        gain[row][col] *= orientation_cosine_[row - 4] / (0.5f * kPi);
      }
    }

    Vector6 correction{};
    for (int row = 0; row < 6; ++row) {
      for (int col = 0; col < 3; ++col) {
        correction[row] += gain[row][col] * residual[col];
      }
    }
    if (converge_flag_) {
      for (int i = 4; i < 6; ++i) {
        correction[i] = Clamp(correction[i], -1e-2f * dt_s, 1e-2f * dt_s);
      }
    }
    correction[3] = 0.0f;

    for (int i = 0; i < 6; ++i) {
      xhat_[i] = xhatminus_[i] + correction[i];
    }
    NormalizeQuaternion(xhat_);
    p_ = UpdateCovariance(gain, h, pminus_);
    return true;
  }

  void UpdateEkf(const App::Vector3f& gyro_radps, const Vector3& accel_mps2,
                 float dt_s) {
    if (dt_s <= 0.0f) {
      return;
    }

    SetTransitionMatrix(gyro_radps, dt_s);
    PredictState();
    SetBiasLinearization(dt_s);
    PredictCovariance(dt_s);

    if (update_count_ == 0) {
      filtered_accel_mps2_ = accel_mps2;
    }
    const float lpf = std::max(config_.accel_lpf_coef, 0.0f);
    for (int i = 0; i < 3; ++i) {
      filtered_accel_mps2_[i] =
          filtered_accel_mps2_[i] * lpf / (dt_s + lpf) +
          accel_mps2[i] * dt_s / (dt_s + lpf);
    }

    const Vector3 measured_accel_direction = Normalize(filtered_accel_mps2_);
    const App::Vector3f corrected_gyro{gyro_radps.x - xhat_[4],
                                       gyro_radps.y - xhat_[5],
                                       gyro_radps.z};
    const bool stable_flag =
        Norm(corrected_gyro) < kGyroStableThresholdRadps &&
        std::fabs(Norm(filtered_accel_mps2_) - kGravityMps2) <
            kAccelStableToleranceMps2;
    ApplyMeasurementUpdate(measured_accel_direction, stable_flag, dt_s);
    state_.gyro_radps = gyro_radps;
    state_.gyro_bias_radps = {xhat_[4], xhat_[5], 0.0f};
    ++update_count_;
  }

  void PredictQuaternionOnly(const App::Vector3f& gyro_radps, float dt_s) {
    if (dt_s <= 0.0f) {
      return;
    }
    SetTransitionMatrix(gyro_radps, dt_s);
    PredictState();
    xhat_ = xhatminus_;
    NormalizeQuaternion(xhat_);
    state_.gyro_radps = gyro_radps;
    state_.gyro_bias_radps = {xhat_[4], xhat_[5], 0.0f};
  }

  void UpdateEulerFromQuaternion() {
    const float q0 = xhat_[0];
    const float q1 = xhat_[1];
    const float q2 = xhat_[2];
    const float q3 = xhat_[3];
    const float roll_rad = std::atan2(2.0f * (q0 * q1 + q2 * q3),
                                      1.0f - 2.0f * (q1 * q1 + q2 * q2));
    const float pitch_sin = Clamp(2.0f * (q0 * q2 - q3 * q1), -1.0f, 1.0f);
    const float pitch_rad = std::asin(pitch_sin);
    const float yaw_rad = std::atan2(2.0f * (q0 * q3 + q1 * q2),
                                     1.0f - 2.0f * (q2 * q2 + q3 * q3));

    state_.yaw_deg = yaw_rad * kRadToDeg;
    state_.pitch_deg = pitch_rad * kRadToDeg;
    state_.roll_deg = roll_rad * kRadToDeg;
  }

  void UpdateStateVectors(const App::Vector3f& accel_g, bool update_yaw_total) {
    state_.quaternion = {xhat_[0], xhat_[1], xhat_[2], xhat_[3]};
    UpdateEulerFromQuaternion();

    if (update_yaw_total) {
      const float yaw_delta = state_.yaw_deg - yaw_angle_last_deg_;
      if (yaw_delta > 180.0f) {
        --yaw_round_count_;
      } else if (yaw_delta < -180.0f) {
        ++yaw_round_count_;
      }
    }
    state_.yaw_total_deg =
        360.0f * static_cast<float>(yaw_round_count_) + state_.yaw_deg;
    yaw_angle_last_deg_ = state_.yaw_deg;

    const App::Vector3f gravity_body = GravityInBody(xhat_);
    const App::Vector3f motion_accel_body_g{accel_g.x - gravity_body.x,
                                            accel_g.y - gravity_body.y,
                                            accel_g.z - gravity_body.z};
    const float lpf = std::max(config_.motion_accel_lpf_coef, 0.0f);
    if (last_dt_s_ > 0.0f) {
      motion_accel_body_lpf_g_.x =
          motion_accel_body_g.x * last_dt_s_ / (lpf + last_dt_s_) +
          motion_accel_body_lpf_g_.x * lpf / (lpf + last_dt_s_);
      motion_accel_body_lpf_g_.y =
          motion_accel_body_g.y * last_dt_s_ / (lpf + last_dt_s_) +
          motion_accel_body_lpf_g_.y * lpf / (lpf + last_dt_s_);
      motion_accel_body_lpf_g_.z =
          motion_accel_body_g.z * last_dt_s_ / (lpf + last_dt_s_) +
          motion_accel_body_lpf_g_.z * lpf / (lpf + last_dt_s_);
    } else {
      motion_accel_body_lpf_g_ = motion_accel_body_g;
    }
    state_.motion_accel_body_g = motion_accel_body_lpf_g_;
    state_.motion_accel_nav_g = BodyToNav(state_.motion_accel_body_g, xhat_);
    state_.motion_accel_world_g = state_.motion_accel_nav_g;
  }

  State state_{};
  Config config_{};
  Vector6 xhat_{};
  Vector6 xhatminus_{};
  Matrix6 f_{};
  Matrix6 p_{};
  Matrix6 pminus_{};
  Vector3 filtered_accel_mps2_{};
  Vector3 orientation_cosine_{};
  Matrix3 install_matrix_{{{1.0f, 0.0f, 0.0f},
                           {0.0f, 1.0f, 0.0f},
                           {0.0f, 0.0f, 1.0f}}};
  App::Vector3f init_accel_sum_g_{};
  App::Vector3f motion_accel_body_lpf_g_{};
  std::uint64_t update_count_ = 0;
  std::uint64_t error_count_ = 0;
  int init_accel_count_ = 0;
  int yaw_round_count_ = 0;
  float yaw_angle_last_deg_ = 0.0f;
  float last_dt_s_ = 0.0f;
  float last_install_yaw_deg_ = 0.0f;
  float last_install_pitch_deg_ = 0.0f;
  float last_install_roll_deg_ = 0.0f;
  bool converge_flag_ = false;
  bool initialized_ = false;
  bool install_param_dirty_ = true;
};

}  // namespace InsAlgorithm
