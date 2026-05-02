#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 博世 BMI088 6 轴惯性测量单元（IMU）的驱动模块 / Driver module for Bosch BMI088 6-axis Inertial Measurement Unit (IMU)
constructor_args:
  - gyro_freq: BMI088::GyroFreq::GYRO_2000HZ_BW230HZ
  - accl_freq: BMI088::AcclFreq::ACCL_800HZ
  - gyro_range: BMI088::GyroRange::DEG_2000DPS
  - accl_range: BMI088::AcclRange::ACCL_6G
  - rotation:
      w: 1.0
      x: 0.0
      y: 0.0
      z: 0.0
  - pid_param:
      k: 1.0
      p: 0.5
      i: 0.01
      d: 0.0
      i_limit: 300.0
      out_limit: 1.0
      cycle: false
  - gyro_topic_name: "bmi088_gyro"
  - accl_topic_name: "bmi088_accl"
  - target_temperature: 45
  - task_stack_depth: 2048
template_args: []
required_hardware: spi_bmi088/spi1/SPI1 bmi088_accl_cs bmi088_gyro_cs bmi088_gyro_int pwm_bmi088_heat ramfs database
depends: []
=== END MANIFEST === */
// clang-format on

/* Recommended Website for calculate rotation:
  https://www.andre-gaschler.com/rotationconverter/ */

#include <array>
#include <cmath>

#include "app_framework.hpp"
#include "BMI088Calibration.hpp"
#include "gpio.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "pid.hpp"
#include "pwm.hpp"
#include "spi.hpp"
#include "transform.hpp"

using LibXR::ErrorCode;

#define BMI088_REG_ACCL_CHIP_ID (0x00)
#define BMI088_REG_ACCL_ERR (0x02)
#define BMI088_REG_ACCL_STATUS (0x03)
#define BMI088_REG_ACCL_X_LSB (0x12)
#define BMI088_REG_ACCL_X_MSB (0x13)
#define BMI088_REG_ACCL_Y_LSB (0x14)
#define BMI088_REG_ACCL_Y_MSB (0x15)
#define BMI088_REG_ACCL_Z_LSB (0x16)
#define BMI088_REG_ACCL_Z_MSB (0x17)
#define BMI088_REG_ACCL_SENSORTIME_0 (0x18)
#define BMI088_REG_ACCL_SENSORTIME_1 (0x19)
#define BMI088_REG_ACCL_SENSORTIME_2 (0x1A)
#define BMI088_REG_ACCL_INT_STAT_1 (0x1D)
#define BMI088_REG_ACCL_TEMP_MSB (0x22)
#define BMI088_REG_ACCL_TEMP_LSB (0x23)
#define BMI088_REG_ACCL_CONF (0x40)
#define BMI088_REG_ACCL_RANGE (0x41)
#define BMI088_REG_ACCL_INT1_IO_CONF (0x53)
#define BMI088_REG_ACCL_INT2_IO_CONF (0x54)
#define BMI088_REG_ACCL_INT1_INT2_MAP_DATA (0x58)
#define BMI088_REG_ACCL_SELF_TEST (0x6D)
#define BMI088_REG_ACCL_PWR_CONF (0x7C)
#define BMI088_REG_ACCL_PWR_CTRL (0x7D)
#define BMI088_REG_ACCL_SOFTRESET (0x7E)

#define BMI088_REG_GYRO_CHIP_ID (0x00)
#define BMI088_REG_GYRO_X_LSB (0x02)
#define BMI088_REG_GYRO_X_MSB (0x03)
#define BMI088_REG_GYRO_Y_LSB (0x04)
#define BMI088_REG_GYRO_Y_MSB (0x05)
#define BMI088_REG_GYRO_Z_LSB (0x06)
#define BMI088_REG_GYRO_Z_MSB (0x07)
#define BMI088_REG_GYRO_INT_STAT_1 (0x0A)
#define BMI088_REG_GYRO_RANGE (0x0F)
#define BMI088_REG_GYRO_BANDWIDTH (0x10)
#define BMI088_REG_GYRO_LPM1 (0x11)
#define BMI088_REG_GYRO_SOFTRESET (0x14)
#define BMI088_REG_GYRO_INT_CTRL (0x15)
#define BMI088_REG_GYRO_INT3_INT4_IO_CONF (0x16)
#define BMI088_REG_GYRO_INT3_INT4_IO_MAP (0x18)
#define BMI088_REG_GYRO_SELF_TEST (0x3C)

#define BMI088_CHIP_ID_ACCL (0x1E)
#define BMI088_CHIP_ID_GYRO (0x0F)

#define BMI088_ACCL_RX_BUFF_LEN (19)
#define BMI088_GYRO_RX_BUFF_LEN (6)

class BMI088 : public LibXR::Application {
 public:
  enum class Device : uint8_t { ACCELMETER, GYROSCOPE };

  enum class GyroRange : uint8_t {
    DEG_2000DPS = 0x00,
    DEG_1000DPS = 0x01,
    DEG_500DPS = 0x02,
    DEG_250DPS = 0x03,
    DEG_125DPS = 0x04
  };

  enum class AcclRange : uint8_t {
    ACCL_3G = 0x00,
    ACCL_6G = 0x01,
    ACCL_12G = 0x02,
    ACCL_24G = 0x03
  };

  enum class GyroFreq : uint8_t {
    GYRO_2000HZ_BW532HZ = 0x00,
    GYRO_2000HZ_BW230HZ = 0x01,
    GYRO_1000HZ_BW116HZ = 0x02,
    GYRO_400HZ_BW46HZ = 0x03,
    GYRO_200HZ_BW23HZ = 0x04,
    GYRO_100HZ_BW12HZ = 0x05,
    GYRO_200HZ_BW64HZ = 0x06,
    GYRO_100HZ_BW32HZ = 0x07,
  };

  enum class AcclFreq : uint8_t {
    ACCL_1600HZ = 0x0C,
    ACCL_800HZ = 0x0B,
    ACCL_400HZ = 0x0A,
    ACCL_200HZ = 0x09,
    ACCL_100HZ = 0x08,
    ACCL_50HZ = 0x07,
    ACCL_25HZ = 0x06,
    ACCL_12_5HZ = 0x05
  };

  static constexpr float M_DEG2RAD_MULT = 0.01745329251f;
  static constexpr float kDefaultGyroStaticDeltaRadps = 0.15f;
  static constexpr float kDefaultGyroMaxAbsMeanRadps = 0.01f;
  static constexpr float kDefaultAccelStaticDeltaG = 0.05f;
  static constexpr float kDefaultAccelNormErrorG = 0.05f;
  static constexpr float kCalibrationTemperatureToleranceC = 2.0f;

  using GyroCalibrationConfig = BMI088Calibration::StaticCalibrationConfig;
  using GyroCalibrationState = BMI088Calibration::StaticCalibrationState;
  using GyroCalibrationStatus = BMI088Calibration::StaticCalibrationStatus;
  enum class GyroCalibrationMode : uint8_t { NONE, STARTUP, THERMAL, MANUAL };

  void Select(Device device) {
    if (device == Device::ACCELMETER) {
      cs_accl_->Write(false);
    } else {
      cs_gyro_->Write(false);
    }
  }

  void Deselect(Device device) {
    if (device == Device::ACCELMETER) {
      cs_accl_->Write(true);
    } else {
      cs_gyro_->Write(true);
    }
  }

  void WriteSingle(Device device, uint8_t reg, uint8_t data) {
    Select(device);
    spi_->MemWrite(reg, data, op_spi_);
    Deselect(device);

    /* For accelmeter, two write operations need at least 2us */
    LibXR::Thread::Sleep(1);
  }

  uint8_t ReadSingle(Device device, uint8_t reg) {
    Select(device);
    spi_->MemRead(reg, {rw_buffer_, 2}, op_spi_);
    Deselect(device);

    if (device == Device::ACCELMETER) {
      return rw_buffer_[1];
    } else {
      return rw_buffer_[0];
    }
  }

  void Read(Device device, uint8_t reg, uint8_t len) {
    Select(device);
    spi_->MemRead(reg, {rw_buffer_, len}, op_spi_);
    Deselect(device);
  }

  BMI088(LibXR::HardwareContainer &hw, LibXR::ApplicationManager &app,
         GyroFreq freq, AcclFreq accl_freq, GyroRange gyro_range,
         AcclRange accl_range, LibXR::Quaternion<float> &&rotation,
         LibXR::PID<float>::Param pid_param, const char *gyro_topic_name,
         const char *accl_topic_name, float target_temperature,
         size_t task_stack_depth)
      : gyro_range_(gyro_range),
        accel_range_(accl_range),
        gyro_freq_(freq),
        accl_freq_(accl_freq),
        target_temperature_(target_temperature),
        topic_gyro_(gyro_topic_name, sizeof(gyro_data_)),
        topic_accl_(accl_topic_name, sizeof(accl_data_)),
        cs_accl_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_accl_cs"})),
        cs_gyro_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_gyro_cs"})),
        int_gyro_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_gyro_int"})),
        spi_(
            hw.template FindOrExit<LibXR::SPI>({"spi_bmi088", "spi1", "SPI1"})),
        pwm_(hw.template FindOrExit<LibXR::PWM>({"pwm_bmi088_heat"})),
        rotation_(std::move(rotation)),
        pid_heat_(pid_param),
        op_spi_(sem_spi_),
        cmd_file_(LibXR::RamFS::CreateFile("bmi088", CommandFunc, this)),
        gyro_data_key_(*hw.template FindOrExit<LibXR::Database>({"database"}),
                       "bmi088_gyro_data",
                       Eigen::Matrix<float, 3, 1>(0.0, 0.0, 0.0)),
        accl_scale_key_(*hw.template FindOrExit<LibXR::Database>({"database"}),
                        "bmi088_accl_scale", 1.0f),
        accl_norm_g_key_(*hw.template FindOrExit<LibXR::Database>({"database"}),
                         "bmi088_accl_norm_g", 1.0f),
        calibration_temperature_key_(
            *hw.template FindOrExit<LibXR::Database>({"database"}),
            "bmi088_calibration_temperature_c", 0.0f) {
    app.Register(*this);

    hw.template FindOrExit<LibXR::RamFS>({"ramfs"})->Add(cmd_file_);

    int_gyro_->DisableInterrupt();

    auto gyro_int_cb = LibXR::GPIO::Callback::Create(
        [](bool in_isr, BMI088 *bmi088) {
          auto time = LibXR::Timebase::GetMicroseconds();
          bmi088->dt_gyro_ = time - bmi088->last_gyro_int_time_;
          bmi088->last_gyro_int_time_ = time;
          bmi088->new_data_.PostFromCallback(in_isr);
        },
        this);

    int_gyro_->SetConfig({.direction = LibXR::GPIO::Direction::FALL_INTERRUPT,
                          .pull = LibXR::GPIO::Pull::NONE});

    int_gyro_->RegisterCallback(gyro_int_cb);

    while (!Init()) {
      XR_LOG_ERROR("BMI088: Init failed. Try again.");
      LibXR::Thread::Sleep(100);
    }

    XR_LOG_PASS("BMI088: Init succeeded.");

    // HeroCode 的 BMI088Init(..., 1) 会在启动阶段建立 GyroOffset。
    // 这里先做启动零偏，不等待恒温目标，保证对外陀螺仪 Topic 尽快扣除当前温度下的零偏。
    if (!RequestStartupGyroCalibration()) {
      XR_LOG_WARN("BMI088: Startup gyro calibration request failed.");
    }

    thread_.Create(this, ThreadFunc, "bmi088_thread", task_stack_depth,
                   LibXR::Thread::Priority::REALTIME);

    void (*temp_ctrl_func)(BMI088 *) = [](BMI088 *bmi088) {
      bmi088->ControlTemperature(0.05f);
    };

    auto temp_ctrl_task = LibXR::Timer::CreateTask(temp_ctrl_func, this, 50);

    LibXR::Timer::Add(temp_ctrl_task);
    LibXR::Timer::Start(temp_ctrl_task);
  }

  bool Init() {
    WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_SOFTRESET, 0xB6);
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_SOFTRESET, 0xB6);

    LibXR::Thread::Sleep(30);

    /* Need to read chip id twice */
    ReadSingle(Device::ACCELMETER, BMI088_REG_ACCL_CHIP_ID);
    ReadSingle(Device::GYROSCOPE, BMI088_REG_GYRO_CHIP_ID);

    auto accl_id = ReadSingle(Device::ACCELMETER, BMI088_REG_ACCL_CHIP_ID);
    auto gyro_id = ReadSingle(Device::GYROSCOPE, BMI088_REG_GYRO_CHIP_ID);

    if (accl_id != BMI088_CHIP_ID_ACCL) {
      return false;
    }
    if (gyro_id != BMI088_CHIP_ID_GYRO) {
      return false;
    }

    /* Accl init. */
    /* 先打开加速度计并进入 active mode，对齐 HeroCode 的初始化表。 */
    WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_PWR_CTRL, 0x04);
    WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_PWR_CONF, 0x00);
    LibXR::Thread::Sleep(50);

    /* 加速度计滤波使用 normal mode，对齐 HeroCode 的 BMI088_ACC_NORMAL。 */
    WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_CONF,
                0x80 | 0x20 | static_cast<uint8_t>(accl_freq_));

    /* 0x00: +-3G. 0x01: +-6G. 0x02: +-12G. 0x03: +-24G. */
    WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_RANGE,
                static_cast<uint8_t>(accel_range_));

    /* Gyro init. */
    /* 0x00: +-2000. 0x01: +-1000. 0x02: +-500. 0x03: +-250. 0x04: +-125. */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_RANGE,
                static_cast<uint8_t>(gyro_range_));

    /* 陀螺仪使用 2000Hz/BW230Hz；bit7 对齐 HeroCode 的 MUST_Set 位。 */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_BANDWIDTH,
                0x80 | static_cast<uint8_t>(gyro_freq_));

    /* 保持 normal mode，对齐 HeroCode 的 BMI088_GYRO_NORMAL_MODE。 */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_LPM1, 0x00);

    /* INT3 and INT4 as output. Push-pull. Active low. */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_INT3_INT4_IO_CONF, 0x00);

    /* Map data ready interrupt to INT3. */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_INT3_INT4_IO_MAP, 0x01);

    /* Enable new data interrupt. */
    WriteSingle(Device::GYROSCOPE, BMI088_REG_GYRO_INT_CTRL, 0x80);

    LibXR::Thread::Sleep(50);
    int_gyro_->EnableInterrupt();

    return true;
  }

  void OnMonitor(void) override {
    if (std::isinf(gyro_data_.x()) || std::isinf(gyro_data_.y()) ||
        std::isinf(gyro_data_.z()) || std::isinf(accl_data_.x()) ||
        std::isinf(accl_data_.y()) || std::isinf(accl_data_.z()) ||
        std::isnan(gyro_data_.x()) || std::isnan(gyro_data_.y()) ||
        std::isnan(gyro_data_.z()) || std::isnan(accl_data_.x()) ||
        std::isnan(accl_data_.y()) || std::isnan(accl_data_.z())) {
      XR_LOG_WARN("BMI088: NaN data detected. gyro: %f %f %f, accl: %f %f %f",
                  gyro_data_.x(), gyro_data_.y(), gyro_data_.z(),
                  accl_data_.x(), accl_data_.y(), accl_data_.z());
    }

    float ideal_gyro_dt = 0.0f;
    switch (gyro_freq_) {
      case GyroFreq::GYRO_2000HZ_BW532HZ:
      case GyroFreq::GYRO_2000HZ_BW230HZ:
        ideal_gyro_dt = 0.0005f;
        break;
      case GyroFreq::GYRO_1000HZ_BW116HZ:
        ideal_gyro_dt = 0.001f;
        break;
      case GyroFreq::GYRO_400HZ_BW46HZ:
        ideal_gyro_dt = 0.0025f;
        break;
      case GyroFreq::GYRO_200HZ_BW23HZ:
      case GyroFreq::GYRO_200HZ_BW64HZ:
        ideal_gyro_dt = 0.005f;
        break;
      case GyroFreq::GYRO_100HZ_BW12HZ:
      case GyroFreq::GYRO_100HZ_BW32HZ:
        ideal_gyro_dt = 0.01f;
        break;
    }

    /* Use other timer as HAL timebase (Because the priority of SysTick is
  lowest) and set the priority to the highest to avoid this issue */
    if (std::fabs(ideal_gyro_dt - dt_gyro_.ToSecondf()) > 0.0003f) {
      XR_LOG_WARN("BMI088 Frequency Error: %6f", dt_gyro_.ToSecondf());
    }
  }

  static void ThreadFunc(BMI088 *bmi088) {
    /* Start PWM */
    if (bmi088->pwm_->SetConfig({1000}) != ErrorCode::OK) {
      XR_LOG_ERROR("BMI088: Heat PWM config failed.");
    }
    if (bmi088->pwm_->SetDutyCycle(0.0f) != ErrorCode::OK) {
      XR_LOG_ERROR("BMI088: Heat PWM duty init failed.");
    }
    if (bmi088->pwm_->Enable() != ErrorCode::OK) {
      XR_LOG_ERROR("BMI088: Heat PWM enable failed.");
    }

    while (true) {
      if (bmi088->new_data_.Wait(50) == ErrorCode::OK) {
        bmi088->RecvAccel();
        bmi088->ParseAccelData();
        bmi088->RecvGyro();
        bmi088->ParseGyroData();
        bmi088->topic_accl_.Publish(bmi088->accl_data_);
        bmi088->topic_gyro_.Publish(bmi088->gyro_data_);
      } else {
        XR_LOG_WARN("BMI088 wait timeout.");
      }
    }
  }

  void ControlTemperature(float dt) {
    auto duty_cycle =
        pid_heat_.Calculate(target_temperature_, temperature_, dt);
    pwm_->SetDutyCycle(duty_cycle);
  }

  void RecvAccel(void) {
    Read(Device::ACCELMETER, BMI088_REG_ACCL_X_LSB, BMI088_ACCL_RX_BUFF_LEN);
  }

  void RecvGyro(void) {
    Read(Device::GYROSCOPE, BMI088_REG_GYRO_X_LSB, BMI088_GYRO_RX_BUFF_LEN);
  }

  bool RequestGyroCalibration(GyroCalibrationConfig config = {}) {
    LibXR::Mutex::LockGuard lock_guard(gyro_calibration_mutex_);
    return StartGyroCalibrationLocked(GyroCalibrationMode::MANUAL, config);
  }

  bool RequestGyroCalibration(
      std::size_t target_samples,
      float max_static_delta_radps = kDefaultGyroStaticDeltaRadps,
      float max_abs_mean_radps = kDefaultGyroMaxAbsMeanRadps) {
    return RequestGyroCalibration(
        {.target_samples = target_samples,
         .gyro_lsb_to_radps = GetGyroLSB() * M_DEG2RAD_MULT,
         .accel_lsb_to_g = GetAcclLSB(),
         .max_gyro_static_delta_radps = max_static_delta_radps,
         .max_accel_static_delta_g = kDefaultAccelStaticDeltaG,
         .max_abs_gyro_mean_radps = max_abs_mean_radps,
         .max_accel_norm_error_g = kDefaultAccelNormErrorG});
  }

  GyroCalibrationStatus GetGyroCalibrationStatus() const {
    LibXR::Mutex::LockGuard lock_guard(gyro_calibration_mutex_);
    return gyro_calibration_.GetStatus();
  }

  void AbortGyroCalibration() {
    LibXR::Mutex::LockGuard lock_guard(gyro_calibration_mutex_);
    gyro_calibration_.Abort();
    gyro_calibration_mode_ = GyroCalibrationMode::NONE;
  }

  float GetAcclLSB(void) {
    switch (accel_range_) {
      case AcclRange::ACCL_24G:
        return 1.0 / 1365.0;
        break;

      case AcclRange::ACCL_12G:
        return 1.0 / 2730.0;
        break;

      case AcclRange::ACCL_6G:
        return 1.0 / 5460.0;
        break;

      case AcclRange::ACCL_3G:
        return 1.0 / 10920.0;
        break;

      default:
        // 非法枚举值按默认最大量程兜底，避免无返回路径。
        return 1.0 / 1365.0;
    }
  }

  void ParseAccelData(void) {
    std::array<int16_t, 3> raw_int16;
    std::array<float, 3> raw;

    float range = GetAcclLSB();

    for (int i = 0; i < 3; i++) {
      raw_int16[i] = static_cast<int16_t>(
          (static_cast<uint8_t>(rw_buffer_[i * 2 + 2]) << 8) |
          static_cast<uint8_t>(rw_buffer_[i * 2 + 1]));
      raw[i] =
          static_cast<float>(raw_int16[i]) * range * accl_scale_key_.data_;
    }
    last_accl_raw_int16_ = raw_int16;
    has_accl_raw_sample_ = true;

    int16_t raw_temp =
        static_cast<int16_t>((static_cast<uint8_t>(rw_buffer_[17]) << 3) |
                             (static_cast<uint8_t>(rw_buffer_[18]) >> 5));
    if (raw_temp > 1023) {
      raw_temp -= 2048;
    }

    temperature_ = static_cast<float>(raw_temp) * 0.125f + 23.0f;

    if (raw[0] == 0.0f && raw[1] == 0.0f && raw[2] == 0.0f) {
      return;
    }

    accl_data_ = rotation_ * Eigen::Matrix<float, 3, 1>(raw[0], raw[1], raw[2]);
  }

  float GetGyroLSB() {
    switch (gyro_range_) {
      case GyroRange::DEG_2000DPS:
        return 1.0 / 16.384;
        break;
      case GyroRange::DEG_1000DPS:
        return 1.0 / 32.768;
        break;
      case GyroRange::DEG_500DPS:
        return 1.0 / 65.536;
        break;
      case GyroRange::DEG_250DPS:
        return 1.0 / 131.072;
        break;
      case GyroRange::DEG_125DPS:
        return 1.0 / 262.144;
        break;

      default:
        // 非法枚举值按默认最大量程兜底，避免无返回路径。
        return 1.0 / 16.384;
    }
  }

  void ParseGyroData(void) {
    std::array<int16_t, 3> raw_int16;
    std::array<float, 3> raw;
    float range = GetGyroLSB();

    for (int i = 0; i < 3; i++) {
      raw_int16[i] = static_cast<int16_t>(
          (static_cast<uint8_t>(rw_buffer_[i * 2 + 1]) << 8) |
          static_cast<uint8_t>(rw_buffer_[i * 2]));
      raw[i] = static_cast<float>(raw_int16[i]) * range * M_DEG2RAD_MULT;
    }

    UpdateGyroCalibration(raw_int16);

    if (raw[0] == 0.0f && raw[1] == 0.0f && raw[2] == 0.0f) {
      return;
    }

    gyro_data_ =
        rotation_ * Eigen::Matrix<float, 3, 1>(
                        Eigen::Matrix<float, 3, 1>(raw[0], raw[1], raw[2]) -
                        gyro_data_key_.data_);
  }

 private:
  bool RequestStartupGyroCalibration() {
    LibXR::Mutex::LockGuard lock_guard(gyro_calibration_mutex_);
    return StartGyroCalibrationLocked(GyroCalibrationMode::STARTUP, {});
  }

  bool StartGyroCalibrationLocked(GyroCalibrationMode mode,
                                  GyroCalibrationConfig config) {
    if (gyro_calibration_.IsRunning()) {
      return false;
    }

    config.gyro_lsb_to_radps = GetGyroLSB() * M_DEG2RAD_MULT;
    config.accel_lsb_to_g = GetAcclLSB();

    if (!gyro_calibration_.Start(config)) {
      gyro_calibration_mode_ = GyroCalibrationMode::NONE;
      return false;
    }

    gyro_calibration_mode_ = mode;
    return true;
  }

  bool TryStartThermalGyroCalibrationLocked() {
    if (thermal_gyro_calibration_attempted_ || thermal_gyro_calibrated_ ||
        !startup_gyro_calibrated_ || gyro_calibration_.IsRunning() ||
        !IsTemperatureNearTarget()) {
      return false;
    }

    thermal_gyro_calibration_attempted_ = true;
    return StartGyroCalibrationLocked(GyroCalibrationMode::THERMAL, {});
  }

  void UpdateGyroCalibration(const std::array<int16_t, 3> &raw_int16) {
    LibXR::Mutex::LockGuard lock_guard(gyro_calibration_mutex_);
    if (!gyro_calibration_.IsRunning()) {
      TryStartThermalGyroCalibrationLocked();
    }

    const auto previous_status = gyro_calibration_.GetStatus();
    const auto active_mode = gyro_calibration_mode_;
    if (previous_status.state == GyroCalibrationState::RUNNING &&
        ShouldDelayGyroCalibrationForTemperature(active_mode)) {
      return;
    }
    if (previous_status.state == GyroCalibrationState::RUNNING &&
        !has_accl_raw_sample_) {
      return;
    }

    const auto status = gyro_calibration_.AddRawSample(
        raw_int16, last_accl_raw_int16_, temperature_);

    if (previous_status.state != GyroCalibrationState::RUNNING) {
      return;
    }

    if (status.state == GyroCalibrationState::SUCCEEDED) {
      gyro_data_key_.data_ = Eigen::Matrix<float, 3, 1>(
          status.gyro_offset_radps[0], status.gyro_offset_radps[1],
          status.gyro_offset_radps[2]);
      accl_scale_key_.data_ = status.accel_scale;
      accl_norm_g_key_.data_ = status.accel_norm_g;
      calibration_temperature_key_.data_ =
          status.temperature_when_calibrated_c;
      if (active_mode == GyroCalibrationMode::STARTUP) {
        startup_gyro_calibrated_ = true;
      } else if (active_mode == GyroCalibrationMode::THERMAL) {
        thermal_gyro_calibrated_ = true;
      }
      gyro_calibration_mode_ = GyroCalibrationMode::NONE;

      const auto gyro_save_result = gyro_data_key_.Set(gyro_data_key_.data_);
      const auto scale_save_result = accl_scale_key_.Set(accl_scale_key_.data_);
      const auto norm_save_result = accl_norm_g_key_.Set(accl_norm_g_key_.data_);
      const auto temp_save_result =
          calibration_temperature_key_.Set(calibration_temperature_key_.data_);
      if (gyro_save_result == LibXR::ErrorCode::OK &&
          scale_save_result == LibXR::ErrorCode::OK &&
          norm_save_result == LibXR::ErrorCode::OK &&
          temp_save_result == LibXR::ErrorCode::OK) {
        XR_LOG_PASS(
            "BMI088: %s calibration succeeded. gyro offset: %f %f %f, accel "
            "norm: %f g, scale: %f, temp: %f C",
            GetGyroCalibrationModeName(active_mode),
            status.gyro_offset_radps[0], status.gyro_offset_radps[1],
            status.gyro_offset_radps[2], status.accel_norm_g,
            status.accel_scale, status.temperature_when_calibrated_c);
      } else {
        XR_LOG_WARN(
            "BMI088: Calibration applied but save failed: gyro=%d scale=%d "
            "norm=%d temp=%d",
            static_cast<int>(gyro_save_result),
            static_cast<int>(scale_save_result),
            static_cast<int>(norm_save_result),
            static_cast<int>(temp_save_result));
      }
    } else if (status.state == GyroCalibrationState::FAILED) {
      XR_LOG_WARN(
          "BMI088: %s calibration failed. reason: %u, gyro_delta: %f, "
          "gyro_mean: %f, accel_delta: %f, accel_norm: %f",
          GetGyroCalibrationModeName(active_mode),
          static_cast<unsigned>(status.failure_reason),
          status.max_gyro_static_delta_radps,
          status.max_abs_gyro_mean_radps,
          status.max_accel_static_delta_g, status.accel_norm_g);
      gyro_calibration_mode_ = GyroCalibrationMode::NONE;
    }
  }

  bool ShouldDelayGyroCalibrationForTemperature(
      GyroCalibrationMode mode) const {
    // 启动零偏用于尽快获得可用零偏，人工 cali 由操作者保证静止；
    // 只有恒温复校需要等待目标温度，避免启动阶段长期裸跑陀螺仪原始值。
    if (mode != GyroCalibrationMode::THERMAL) {
      return false;
    }
    return !IsTemperatureNearTarget();
  }

  bool IsTemperatureNearTarget() const {
    if (target_temperature_ <= 0.0f) {
      return false;
    }
    if (temperature_ <= 0.0f) {
      return false;
    }
    return std::fabs(temperature_ - target_temperature_) <=
           kCalibrationTemperatureToleranceC;
  }

  static const char *GetGyroCalibrationModeName(GyroCalibrationMode mode) {
    switch (mode) {
      case GyroCalibrationMode::STARTUP:
        return "startup";
      case GyroCalibrationMode::THERMAL:
        return "thermal";
      case GyroCalibrationMode::MANUAL:
        return "manual";
      case GyroCalibrationMode::NONE:
      default:
        return "unknown";
    }
  }

  static void PrintGyroCalibrationStatus(const GyroCalibrationStatus &status) {
    LibXR::STDIO::Printf<"Gyro calibration state: ">();
    switch (status.state) {
      case GyroCalibrationState::IDLE:
        LibXR::STDIO::Printf<"idle">();
        break;
      case GyroCalibrationState::RUNNING:
        LibXR::STDIO::Printf<"running">();
        break;
      case GyroCalibrationState::SUCCEEDED:
        LibXR::STDIO::Printf<"succeeded">();
        break;
      case GyroCalibrationState::FAILED:
        LibXR::STDIO::Printf<"failed">();
        break;
    }
    LibXR::STDIO::Printf<", samples: %u, gyro offset: %f %f %f, gyro delta: %f, gyro mean: %f, accel delta: %f, accel norm: %f, accel scale: %f, temp: %f, reason: %u\r\n">(
        static_cast<unsigned>(status.sample_count),
        status.gyro_offset_radps[0], status.gyro_offset_radps[1],
        status.gyro_offset_radps[2], status.max_gyro_static_delta_radps,
        status.max_abs_gyro_mean_radps, status.max_accel_static_delta_g,
        status.accel_norm_g, status.accel_scale,
        status.temperature_when_calibrated_c,
        static_cast<unsigned>(status.failure_reason));
  }

  static int CommandFunc(BMI088 *bmi088, int argc, char **argv) {
    if (argc == 1) {
      LibXR::STDIO::Printf<"Usage:\r\n">();
      LibXR::STDIO::Printf<"  show [time_ms] [interval_ms] - Print sensor data periodically.\r\n">();
      LibXR::STDIO::Printf<"  list_offset                  - Show current gyro calibration offset.\r\n">();
      LibXR::STDIO::Printf<"  list_accl_scale              - Show current accelerometer calibration scale.\r\n">();
      LibXR::STDIO::Printf<"  cali                         - Start non-blocking gyroscope calibration.\r\n">();
      LibXR::STDIO::Printf<"  cali_status                  - Show gyroscope calibration status.\r\n">();
      LibXR::STDIO::Printf<"  cali_abort                   - Abort gyroscope calibration.\r\n">();
    } else if (argc == 2) {
      if (strcmp(argv[1], "list_offset") == 0) {
        LibXR::STDIO::Printf<"Current calibration offset - x: %f, y: %f, z: %f\r\n">(
            bmi088->gyro_data_key_.data_.x(), bmi088->gyro_data_key_.data_.y(),
            bmi088->gyro_data_key_.data_.z());
      } else if (strcmp(argv[1], "list_accl_scale") == 0) {
        LibXR::STDIO::Printf<"Current accel calibration - norm: %f g, scale: %f, temp: %f C\r\n">(
            bmi088->accl_norm_g_key_.data_, bmi088->accl_scale_key_.data_,
            bmi088->calibration_temperature_key_.data_);
      } else if (strcmp(argv[1], "cali") == 0) {
        if (bmi088->RequestGyroCalibration()) {
          LibXR::STDIO::Printf<"Starting BMI088 static calibration. Please keep the device steady.\r\n">();
        } else {
          LibXR::STDIO::Printf<"BMI088 calibration is already running or invalid.\r\n">();
        }
        PrintGyroCalibrationStatus(bmi088->GetGyroCalibrationStatus());
      } else if (strcmp(argv[1], "cali_status") == 0) {
        PrintGyroCalibrationStatus(bmi088->GetGyroCalibrationStatus());
      } else if (strcmp(argv[1], "cali_abort") == 0) {
        bmi088->AbortGyroCalibration();
        LibXR::STDIO::Printf<"Gyroscope calibration aborted.\r\n">();
      }
    } else if (argc == 4) {
      if (strcmp(argv[1], "show") == 0) {
        int time = std::atoi(argv[2]);
        int delay = std::atoi(argv[3]);

        delay = std::clamp(delay, 2, 1000);

        while (time > 0) {
          LibXR::STDIO::Printf<"Accel: x = %+5f, y = %+5f, z = %+5f | Gyro: x = %+5f, y = %+5f, z = %+5f | Temp: %+5f\r\n">(
              bmi088->accl_data_.x(), bmi088->accl_data_.y(),
              bmi088->accl_data_.z(), bmi088->gyro_data_.x(),
              bmi088->gyro_data_.y(), bmi088->gyro_data_.z(),
              bmi088->temperature_);
          LibXR::Thread::Sleep(delay);
          time -= delay;
        }
      }
    } else {
      LibXR::STDIO::Printf<"Error: Invalid arguments.\r\n">();
      return -1;
    }

    return 0;
  }

  GyroRange gyro_range_ = GyroRange::DEG_2000DPS;
  AcclRange accel_range_ = AcclRange::ACCL_6G;
  GyroFreq gyro_freq_ = GyroFreq::GYRO_2000HZ_BW230HZ;
  AcclFreq accl_freq_ = AcclFreq::ACCL_800HZ;

  BMI088Calibration::StaticCalibrationAccumulator gyro_calibration_;
  mutable LibXR::Mutex gyro_calibration_mutex_;
  GyroCalibrationMode gyro_calibration_mode_ = GyroCalibrationMode::NONE;
  bool startup_gyro_calibrated_ = false;
  bool thermal_gyro_calibration_attempted_ = false;
  bool thermal_gyro_calibrated_ = false;

  float temperature_ = 0.0f;

  LibXR::MicrosecondTimestamp last_gyro_int_time_ = 0;
  LibXR::MicrosecondTimestamp::Duration dt_gyro_ = 0;

  float target_temperature_ = 25.0f;

  uint8_t rw_buffer_[20];
  Eigen::Matrix<float, 3, 1> gyro_data_, accl_data_;
  std::array<int16_t, 3> last_accl_raw_int16_ = {0, 0, 0};
  bool has_accl_raw_sample_ = false;
  LibXR::Topic topic_gyro_, topic_accl_;
  LibXR::GPIO *cs_accl_, *cs_gyro_, *int_gyro_;
  LibXR::SPI *spi_;
  LibXR::PWM *pwm_;

  LibXR::Quaternion<float> rotation_;

  LibXR::PID<float> pid_heat_;
  LibXR::Semaphore sem_spi_, new_data_;
  LibXR::SPI::OperationRW op_spi_;

  LibXR::RamFS::File cmd_file_;

  LibXR::Database::Key<Eigen::Matrix<float, 3, 1>> gyro_data_key_;
  LibXR::Database::Key<float> accl_scale_key_;
  LibXR::Database::Key<float> accl_norm_g_key_;
  LibXR::Database::Key<float> calibration_temperature_key_;

  LibXR::Thread thread_;
};
