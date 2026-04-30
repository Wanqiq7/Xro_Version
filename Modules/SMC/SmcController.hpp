// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 滑模控制器（SMC），提供5种滑模模式的位置/速度控制算法
constructor_args: []
template_args: []
required_hardware: []
depends: []
=== END MANIFEST === */
// clang-format on

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Module {

enum class SmcMode : std::uint8_t {
  kExponent,
  kPower,
  kTfsmc,
  kVelSmc,
  kEismc
};

struct SmcParam {
  float J = 0.0f;
  float K = 0.0f;
  float c = 0.0f;
  float c1 = 0.0f;
  float c2 = 0.0f;
  float p = 0.0f;
  float q = 0.0f;
  float beta = 0.0f;
  float epsilon = 0.0f;
  float limit = 1.0f;
  float u_max = 0.0f;
  float dead_zone = 0.0f;
  SmcMode mode = SmcMode::kExponent;
};

class SmcController {
 public:
  SmcController() = default;
  explicit SmcController(const SmcParam& param) { SetParam(param); }

  void SetParam(const SmcParam& param) {
    param_ = param;
    OutContinuation();
  }

  const SmcParam& Param() const { return param_; }

  float Calculate(float target, float pos_fb, float vel_fb, float dt) {
    UpdateErrorPosition(target, pos_fb, vel_fb, dt);
    switch (param_.mode) {
      case SmcMode::kExponent: return CalculateExponent();
      case SmcMode::kPower:    return CalculatePower();
      case SmcMode::kTfsmc:    return CalculateTfsmc();
      case SmcMode::kVelSmc:   return CalculateVelSmc();
      case SmcMode::kEismc:    return CalculateEismc();
    }
    return 0.0f;
  }

  float Calculate(float target, float vel_fb, float dt) {
    UpdateErrorVelocity(target, vel_fb, dt);
    switch (param_.mode) {
      case SmcMode::kVelSmc: return CalculateVelSmc();
      default: return 0.0f;
    }
  }

  void Reset() {
    target_ = 0.0f;
    target_last_ = 0.0f;
    target_derivative_ = 0.0f;
    target_derivative_last_ = 0.0f;
    target_second_derivative_ = 0.0f;
    pos_error_ = 0.0f;
    vel_error_ = 0.0f;
    pos_error_integral_ = 0.0f;
    vel_error_integral_ = 0.0f;
    tfsmc_pos_pow_ = 0.0f;
    output_ = 0.0f;
    sliding_surface_ = 0.0f;
  }

  void ClearIntegral() {
    pos_error_integral_ = 0.0f;
    vel_error_integral_ = 0.0f;
  }

  float Output() const { return output_; }
  float SlidingSurface() const { return sliding_surface_; }

 private:
  float CalculateExponent() {
    if (std::abs(pos_error_) < param_.dead_zone) {
      pos_error_ = 0.0f;
      output_ = 0.0f;
      return 0.0f;
    }

    sliding_surface_ = param_.c * pos_error_ + vel_error_;
    float fun = Sat(sliding_surface_);

    float u = param_.J * (-param_.c * vel_error_ - param_.K * sliding_surface_ - param_.epsilon * fun);

    if (u > param_.u_max) u = param_.u_max;
    if (u < -param_.u_max) u = -param_.u_max;
    output_ = u;
    return u;
  }

  float CalculatePower() {
    if (std::abs(pos_error_) < param_.dead_zone) {
      pos_error_ = 0.0f;
      output_ = 0.0f;
      return 0.0f;
    }

    sliding_surface_ = param_.c * pos_error_ + vel_error_;
    float fun = Sat(sliding_surface_);

    float u = param_.J * (-param_.c * vel_error_ - param_.K * sliding_surface_
                          - param_.K * std::pow(std::abs(sliding_surface_), param_.epsilon) * fun);

    if (u > param_.u_max) u = param_.u_max;
    if (u < -param_.u_max) u = -param_.u_max;
    output_ = u;

    return u;
  }

  float CalculateTfsmc() {
    if (std::abs(pos_error_) < param_.dead_zone) {
      pos_error_ = 0.0f;
      output_ = 0.0f;
      return 0.0f;
    }

    tfsmc_pos_pow_ = std::pow(std::abs(pos_error_), param_.q / param_.p);
    if (pos_error_ <= 0.0f) tfsmc_pos_pow_ = -tfsmc_pos_pow_;

    sliding_surface_ = param_.beta * tfsmc_pos_pow_ + vel_error_;
    float fun = Sat(sliding_surface_);

    float u = 0.0f;
    if (pos_error_ != 0.0f) {
      u = param_.J * (target_second_derivative_
                      - param_.K * sliding_surface_
                      - param_.epsilon * fun
                      - vel_error_ * ((param_.q * param_.beta) * tfsmc_pos_pow_) / (param_.p * pos_error_));
    }

    if (u > param_.u_max) u = param_.u_max;
    if (u < -param_.u_max) u = -param_.u_max;
    output_ = u;

    return u;
  }

  float CalculateVelSmc() {
    sliding_surface_ = vel_error_ + param_.c * vel_error_integral_;
    float fun = Sat(sliding_surface_);

    float u = param_.J * (target_derivative_ - param_.c * vel_error_ - param_.K * sliding_surface_ - param_.epsilon * fun);

    if (u > param_.u_max) u = param_.u_max;
    if (u < -param_.u_max) u = -param_.u_max;
    output_ = u;
    return u;
  }

  float CalculateEismc() {
    if (std::abs(pos_error_) < param_.dead_zone) {
      pos_error_ = 0.0f;
      output_ = 0.0f;
      return 0.0f;
    }

    sliding_surface_ = param_.c1 * pos_error_ + vel_error_ + param_.c2 * pos_error_integral_;
    float fun = Sat(sliding_surface_);

    float u = param_.J * (-param_.c1 * vel_error_ - param_.c2 * pos_error_ - param_.K * sliding_surface_ - param_.epsilon * fun);

    if (u > param_.u_max) u = param_.u_max;
    if (u < -param_.u_max) u = -param_.u_max;
    output_ = u;

    return u;
  }

  float Sat(float s) const {
    if (param_.epsilon == 0.0f) return Sign(s);
    float y = s / param_.epsilon;
    if (std::abs(y) <= param_.limit) return y;
    return Sign(y);
  }

  static float Sign(float s) {
    if (s > 0.0f) return 1.0f;
    if (s < 0.0f) return -1.0f;
    return 0.0f;
  }

  void UpdateErrorPosition(float target, float pos_fb, float vel_fb, float dt) {
    target_ = target;
    target_derivative_ = (target_ - target_last_) / dt;
    target_second_derivative_ = (target_derivative_ - target_derivative_last_) / dt;

    pos_error_ = pos_fb - target;
    vel_error_ = vel_fb - target_derivative_;

    target_last_ = target_;
    target_derivative_last_ = target_derivative_;

    pos_error_integral_ += pos_error_ * dt;
  }

  void UpdateErrorVelocity(float target, float vel_fb, float dt) {
    target_ = target;
    target_derivative_ = (target_ - target_last_) / dt;

    vel_error_ = vel_fb - target;

    vel_error_integral_ += vel_error_ * dt;

    target_last_ = target_;
  }

  void OutContinuation() {
    if (param_last_.K != 0.0f && param_.K != 0.0f) {
      if (param_last_.c2 != 0.0f && param_.c2 != 0.0f) {
        pos_error_integral_ *= (param_last_.K / param_.K) * (param_last_.c2 / param_.c2);
      }
      if (param_last_.c != 0.0f && param_.c != 0.0f) {
        vel_error_integral_ *= (param_last_.K / param_.K) * (param_last_.c / param_.c);
      }
    }
    param_last_ = param_;
  }

  SmcParam param_{};
  SmcParam param_last_{};

  float target_ = 0.0f;
  float target_last_ = 0.0f;
  float target_derivative_ = 0.0f;
  float target_derivative_last_ = 0.0f;
  float target_second_derivative_ = 0.0f;
  float pos_error_ = 0.0f;
  float vel_error_ = 0.0f;
  float pos_error_integral_ = 0.0f;
  float vel_error_integral_ = 0.0f;
  float tfsmc_pos_pow_ = 0.0f;

  float output_ = 0.0f;
  float sliding_surface_ = 0.0f;
};

}  // namespace Module
