#include "../../../Modules/SMC/SmcController.hpp"

// 验证默认构造
static void TestDefaultConstruction() {
  Module::SmcController smc;
  (void)smc;
}

// 验证参数构造
static void TestParamConstruction() {
  Module::SmcParam param;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.epsilon = 1.0f;
  param.limit = 1.0f;
  param.u_max = 100.0f;
  param.dead_zone = 0.01f;
  param.mode = Module::SmcMode::kExponent;

  Module::SmcController smc(param);
  (void)smc;
}

// 验证 SetParam / Param
static void TestSetParam() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.mode = Module::SmcMode::kExponent;
  param.J = 1.0f;
  param.K = 5.0f;
  param.c = 3.0f;
  param.epsilon = 0.5f;
  param.limit = 1.0f;
  param.u_max = 50.0f;
  param.dead_zone = 0.001f;

  smc.SetParam(param);
  const auto& p = smc.Param();
  (void)p;
}

// 验证位置控制 Calculate（双反馈）
static void TestCalculatePosition() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.mode = Module::SmcMode::kExponent;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  param.dead_zone = 0.001f;
  smc.SetParam(param);

  float u = smc.Calculate(0.0f, 0.1f, 0.2f, 0.002f);
  (void)u;
}

// 验证速度控制 Calculate（单反馈）
static void TestCalculateVelocity() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.mode = Module::SmcMode::kVelSmc;
  param.J = 0.5f;
  param.K = 5.0f;
  param.c = 2.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  smc.SetParam(param);

  float u = smc.Calculate(100.0f, 95.0f, 0.002f);
  (void)u;
}

// 验证 5 种模式都能调用
static void TestAllModes() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.c1 = 3.0f;
  param.c2 = 0.1f;
  param.p = 5.0f;
  param.q = 3.0f;
  param.beta = 2.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  param.dead_zone = 0.001f;

  // Exponent
  param.mode = Module::SmcMode::kExponent;
  smc.SetParam(param);
  float u1 = smc.Calculate(0.0f, 0.5f, 0.1f, 0.002f);
  (void)u1;

  // Power
  param.mode = Module::SmcMode::kPower;
  param.epsilon = 0.5f; // Power 模式下 epsilon 是幂次指数
  smc.SetParam(param);
  float u2 = smc.Calculate(0.0f, 0.5f, 0.1f, 0.002f);
  (void)u2;

  // Tfsmc
  param.mode = Module::SmcMode::kTfsmc;
  param.epsilon = 1.0f; // 恢复为切换增益
  smc.SetParam(param);
  float u3 = smc.Calculate(0.0f, 0.5f, 0.1f, 0.002f);
  (void)u3;

  // VelSmc
  param.mode = Module::SmcMode::kVelSmc;
  smc.SetParam(param);
  float u4 = smc.Calculate(100.0f, 95.0f, 0.002f);
  (void)u4;

  // Eismc
  param.mode = Module::SmcMode::kEismc;
  smc.SetParam(param);
  float u5 = smc.Calculate(0.0f, 0.5f, 0.1f, 0.002f);
  (void)u5;
}

// 验证 Reset / ClearIntegral
static void TestResetAndClearIntegral() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  smc.SetParam(param);

  smc.Calculate(0.0f, 1.0f, 0.5f, 0.002f);
  smc.Reset();
  smc.Calculate(0.0f, 1.0f, 0.5f, 0.002f);
  smc.ClearIntegral();
}

// 验证死区：小误差应返回 0
static void TestDeadZone() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.mode = Module::SmcMode::kExponent;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  param.dead_zone = 0.01f;
  smc.SetParam(param);

  float u = smc.Calculate(0.0f, 0.001f, 0.0f, 0.002f); // 误差 < dead_zone
  (void)u;
}

// 验证 Output / SlidingSurface
static void TestAccessors() {
  Module::SmcController smc;
  Module::SmcParam param;
  param.J = 0.5f;
  param.K = 10.0f;
  param.c = 5.0f;
  param.epsilon = 1.0f;
  param.u_max = 100.0f;
  smc.SetParam(param);

  smc.Calculate(0.0f, 0.5f, 0.2f, 0.002f);
  float out = smc.Output();
  float s = smc.SlidingSurface();
  (void)out;
  (void)s;
}

// 主函数（不需要实际运行，只需要编译通过）
int main() {
  TestDefaultConstruction();
  TestParamConstruction();
  TestSetParam();
  TestCalculatePosition();
  TestCalculateVelocity();
  TestAllModes();
  TestResetAndClearIntegral();
  TestDeadZone();
  TestAccessors();
  return 0;
}
