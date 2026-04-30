#pragma once

#include <cmath>
#include <cstdint>

namespace Module {
namespace RLS {

/// @brief 2维递推最小二乘（RLS）估计器，带遗忘因子
///
/// 用于在线估计机器人底盘功率模型的未知参数：
///   P_loss = k1 * |ω| + k2 * τ² + k3
///
/// 其中 k3（待机损耗）为预设常量，k1（转速损耗系数）和 k2（力矩平方损耗系数）
/// 由此估计器在线辨识。
///
/// 算法特点：
///   - 纯标量实现，零外部依赖（不依赖 CMSIS-DSP、Eigen 等矩阵库）
///   - 显式遗忘因子 (0, 1]，自适应时变参数
///   - 参数保持正值约束（>= 1e-5）
class RlsEstimator {
 public:
  struct Config {
    float delta = 1e-5f;       // 协方差矩阵 P 的初始对角值
    float lambda = 0.9999f;    // 遗忘因子，取值范围 (0, 1]
  };

  struct State {
    float k1 = 0.22f;  // 当前估计的转速损耗系数 (W/(rad/s))
    float k2 = 1.2f;   // 当前估计的力矩平方损耗系数 (W/(Nm²))
  };

  explicit RlsEstimator(Config config)
      : config_(config),
        p11_(config.delta),
        p12_(0.0f),
        p21_(0.0f),
        p22_(config.delta) {}

  /// @brief 手动设置初始参数估计值（同时重置协方差矩阵）
  inline void SetParams(const float k1_init, const float k2_init) {
    state_.k1 = k1_init;
    state_.k2 = k2_init;
    update_count_ = 0;
    // 重置 P 矩阵为单位阵 / 缩放（等价于初始 delta 对角）
    p11_ = config_.delta;
    p12_ = 0.0f;
    p21_ = 0.0f;
    p22_ = config_.delta;
  }

  /// @brief 执行一次 RLS 递推更新
  ///
  /// @param omega_sum   四个电机的 |ω| 之和 (rad/s)
  /// @param tau_sq_sum  四个电机的 τ² 之和 (Nm²)
  /// @param measured_loss  实测功率损耗 = 实测功率 − 有效机械功率 − k3 (W)
  /// @param dt_s        时间步长（保留参数，当前未使用）
  /// @return 更新后的参数估计 (k1, k2)
  inline State Update(const float omega_sum, const float tau_sq_sum,
                      const float measured_loss, const float dt_s = 0.0f) {
    // 1. 计算标量 S = lambda + phi^T * P * phi
    //    phi = [omega_sum, tau_sq_sum]^T
    const float omega_sq = omega_sum * omega_sum;
    const float tau_sq_sq = tau_sq_sum * tau_sq_sum;
    const float omega_tau = omega_sum * tau_sq_sum;

    float S = config_.lambda
             + omega_sq * p11_
             + omega_tau * (p12_ + p21_)
             + tau_sq_sq * p22_;

    // 防止除以零
    if (S < 1e-9f) {
      S = 1e-9f;
    }

    // 2. 计算增益向量 K = P * phi / S (2x1)
    const float K0 = (p11_ * omega_sum + p12_ * tau_sq_sum) / S;
    const float K1 = (p21_ * omega_sum + p22_ * tau_sq_sum) / S;

    // 3. 计算先验预测误差
    //    e = y - phi^T * theta_old
    const float prediction = state_.k1 * omega_sum + state_.k2 * tau_sq_sum;
    const float e = measured_loss - prediction;

    // 4. 更新参数估计: theta_new = theta_old + K * e
    float k1_new = state_.k1 + K0 * e;
    float k2_new = state_.k2 + K1 * e;

    // 5. 强制保持参数为正（物理意义约束）
    state_.k1 = std::fmax(k1_new, 1e-5f);
    state_.k2 = std::fmax(k2_new, 1e-5f);

    // 6. 更新协方差矩阵: P = (P - K * phi^T * P) / lambda
    //    展开为标量四则运算
    const float temp0 = omega_sum * p11_ + tau_sq_sum * p21_;
    const float temp1 = omega_sum * p12_ + tau_sq_sum * p22_;

    p11_ = (p11_ - K0 * temp0) / config_.lambda;
    p12_ = (p12_ - K0 * temp1) / config_.lambda;
    p21_ = (p21_ - K1 * temp0) / config_.lambda;
    p22_ = (p22_ - K1 * temp1) / config_.lambda;

    ++update_count_;
    return state_;
  }

  /// @brief 完全重置 RLS 状态（重置 P 矩阵为初始值、清零计数）
  inline void Reset() {
    p11_ = config_.delta;
    p12_ = 0.0f;
    p21_ = 0.0f;
    p22_ = config_.delta;
    state_.k1 = 0.22f;
    state_.k2 = 1.2f;
    update_count_ = 0;
  }

  /// @brief 获取当前估计参数
  constexpr State GetParams() const { return state_; }

  /// @brief 获取已执行的更新次数
  constexpr uint32_t GetUpdateCount() const { return update_count_; }

 private:
  Config config_;
  State state_;

  // 协方差矩阵 P (2x2)，按二维数组展开存储
  float p11_, p12_;
  float p21_, p22_;

  // 累计更新次数
  uint32_t update_count_ = 0;
};

}  // namespace RLS
}  // namespace Module
