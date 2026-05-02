#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 基于 BMI088 Topic 的 donor-style QuaternionEKF INS / 姿态观测模块
constructor_args:
  - gyro_topic_name: "bmi088_gyro"
  - accl_topic_name: "bmi088_accl"
  - ins_topic_name: "ins_state"
  - task_stack_depth: 2048
template_args: []
required_hardware:
depends: []
=== END MANIFEST === */
// clang-format on

#include <cmath>

#include "app_framework.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "../../App/Topics/InsState.hpp"
#include "QuaternionInsEstimator.hpp"

class INS : public LibXR::Application {
 public:
  using ImuVector = Eigen::Matrix<float, 3, 1>;

  INS(LibXR::ApplicationManager& app, const char* gyro_topic_name,
      const char* accl_topic_name, const char* ins_topic_name,
      LibXR::Topic::Domain* output_domain = nullptr,
      std::size_t task_stack_depth = 2048)
      : ins_topic_(LibXR::Topic::CreateTopic<App::InsState>(
            ins_topic_name, output_domain, false, true, true)),
        gyro_subscriber_(gyro_topic_name),
        accl_subscriber_(accl_topic_name) {
    gyro_subscriber_.StartWaiting();
    accl_subscriber_.StartWaiting();
    thread_.Create(this, ThreadFunc, "ins_thread", task_stack_depth,
                   LibXR::Thread::Priority::REALTIME);
    app.Register(*this);
  }

  void OnMonitor() override {
    App::InsState publish_state;
    {
      LibXR::Mutex::LockGuard lock_guard(state_mutex_);
      publish_state = state_;
    }
    ins_topic_.Publish(publish_state);
  }

 private:
  static constexpr float kRadToDeg = 57.29577951308232f;
  static constexpr std::uint32_t kSensorOnlineTimeoutMs = 100;

  static void ThreadFunc(INS* ins) {
    while (true) {
      const auto now_us = LibXR::Timebase::GetMicroseconds();
      const auto now_ms = LibXR::Timebase::GetMilliseconds();
      const bool fresh_gyro = ins->PullSamples(now_us);
      ins->UpdateAttitude(fresh_gyro);
      ins->RefreshStateSnapshot(now_ms);
      LibXR::Thread::Sleep(1);
    }
  }

  bool PullSamples(LibXR::MicrosecondTimestamp now_us) {
    bool fresh_gyro = false;
    if (gyro_subscriber_.Available()) {
      gyro_data_ = gyro_subscriber_.GetData();
      gyro_subscriber_.StartWaiting();
      last_gyro_update_us_ = now_us;
      last_gyro_update_ms_ = static_cast<std::uint32_t>(
          LibXR::Timebase::GetMilliseconds());
      has_gyro_sample_ = true;
      fresh_gyro = true;
    }

    if (accl_subscriber_.Available()) {
      accl_data_ = accl_subscriber_.GetData();
      accl_subscriber_.StartWaiting();
      last_accl_update_us_ = now_us;
      last_accl_update_ms_ = static_cast<std::uint32_t>(
          LibXR::Timebase::GetMilliseconds());
      has_accl_sample_ = true;
    }

    return fresh_gyro;
  }

  void UpdateAttitude(bool fresh_gyro) {
    if (!fresh_gyro || !has_gyro_sample_ || !has_accl_sample_) {
      return;
    }

    float dt_s = 0.0f;
    if (last_attitude_update_us_ != 0) {
      dt_s = (last_gyro_update_us_ - last_attitude_update_us_).ToSecondf();
    }
    last_attitude_update_us_ = last_gyro_update_us_;

    estimator_.Update(ToVector3f(gyro_data_), ToVector3f(accl_data_), dt_s);
  }

  void RefreshStateSnapshot(LibXR::MillisecondTimestamp now_ms) {
    const std::uint32_t now = static_cast<std::uint32_t>(now_ms);
    App::InsState next_state{};
    const bool estimator_ready = estimator_.IsInitialized();
    next_state.gyro_online =
        estimator_ready &&
        has_gyro_sample_ &&
        static_cast<std::uint32_t>(now - last_gyro_update_ms_) <=
            kSensorOnlineTimeoutMs;
    next_state.accl_online =
        estimator_ready &&
        has_accl_sample_ &&
        static_cast<std::uint32_t>(now - last_accl_update_ms_) <=
            kSensorOnlineTimeoutMs;
    next_state.online =
        estimator_ready && next_state.gyro_online && next_state.accl_online;

    const auto& estimator_state = estimator_.GetState();
    next_state.yaw_rate_degps =
        estimator_state.corrected_gyro_radps.z * kRadToDeg;
    next_state.pitch_rate_degps =
        estimator_state.corrected_gyro_radps.x * kRadToDeg;
    next_state.roll_rate_degps =
        estimator_state.corrected_gyro_radps.y * kRadToDeg;
    next_state.yaw_deg = estimator_state.yaw_deg;
    next_state.pitch_deg = estimator_state.pitch_deg;
    next_state.roll_deg = estimator_state.roll_deg;
    next_state.yaw_total_deg = estimator_state.yaw_total_deg;
    next_state.quaternion = estimator_state.quaternion;
    next_state.gyro_bias_radps = estimator_state.gyro_bias_radps;
    next_state.corrected_gyro_radps = estimator_state.corrected_gyro_radps;
    next_state.motion_accel_body_g = estimator_state.motion_accel_body_g;
    next_state.motion_accel_nav_g = estimator_state.motion_accel_nav_g;
    next_state.motion_accel_world_g = estimator_state.motion_accel_world_g;
    next_state.motion_accl_x = estimator_state.motion_accel_body_g.x;
    next_state.motion_accl_y = estimator_state.motion_accel_body_g.y;
    next_state.motion_accl_z = estimator_state.motion_accel_body_g.z;

    next_state.last_update_ms = now;

    LibXR::Mutex::LockGuard lock_guard(state_mutex_);
    state_ = next_state;
  }

  static App::Vector3f ToVector3f(const ImuVector& vector) {
    return {vector.x(), vector.y(), vector.z()};
  }

  LibXR::Topic ins_topic_;
  LibXR::Topic::ASyncSubscriber<ImuVector> gyro_subscriber_;
  LibXR::Topic::ASyncSubscriber<ImuVector> accl_subscriber_;
  LibXR::Thread thread_;
  LibXR::Mutex state_mutex_;
  InsAlgorithm::QuaternionInsEstimator estimator_;

  ImuVector gyro_data_{0.0f, 0.0f, 0.0f};
  ImuVector accl_data_{0.0f, 0.0f, 1.0f};
  App::InsState state_{};

  LibXR::MicrosecondTimestamp last_gyro_update_us_ = 0;
  LibXR::MicrosecondTimestamp last_accl_update_us_ = 0;
  LibXR::MicrosecondTimestamp last_attitude_update_us_ = 0;
  std::uint32_t last_gyro_update_ms_ = 0;
  std::uint32_t last_accl_update_ms_ = 0;
  bool has_gyro_sample_ = false;
  bool has_accl_sample_ = false;
};
