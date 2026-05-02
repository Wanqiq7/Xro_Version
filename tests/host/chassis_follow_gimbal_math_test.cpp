#include <cassert>

#include "App/Chassis/ChassisControlMath.hpp"

namespace {

void ExpectFollowGimbalIgnoresIntegratedInsYaw() {
  App::GimbalState gimbal_state{};
  gimbal_state.online = true;
  gimbal_state.yaw_deg = 45.0f;
  gimbal_state.relative_yaw_deg = -3.5f;

  assert(App::SelectFollowGimbalYawErrorDeg(gimbal_state) == -3.5f);
}

}  // namespace

int main() {
  ExpectFollowGimbalIgnoresIntegratedInsYaw();
  return 0;
}
