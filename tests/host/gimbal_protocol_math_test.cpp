#include <cassert>
#include <cmath>

#include "App/Gimbal/GimbalMath.hpp"

namespace {

bool Near(float actual, float expected, float tolerance) {
  return std::fabs(actual - expected) <= tolerance;
}

void ExpectVisionWireAnglesUseRadiansAndAppUsesDegrees() {
  assert(Near(App::GimbalMath::RadiansToDegrees(1.0f), 57.29578f, 0.0001f));
  assert(Near(App::GimbalMath::DegreesToRadians(180.0f), 3.1415927f, 0.0001f));
}

void ExpectRelativeYawUsesMotorSingleRoundAngleAndMechanicalAlign() {
  assert(Near(App::GimbalMath::ComputeRelativeYawDeg(10.0f, 350.0f), 20.0f,
              0.0001f));
  assert(Near(App::GimbalMath::ComputeRelativeYawDeg(350.0f, 10.0f), -20.0f,
              0.0001f));
  assert(Near(App::GimbalMath::ComputeRelativeYawDeg(20.0f, 40.0f, -1.0f),
              20.0f, 0.0001f));
}

}  // namespace

int main() {
  ExpectVisionWireAnglesUseRadiansAndAppUsesDegrees();
  ExpectRelativeYawUsesMotorSingleRoundAngleAndMechanicalAlign();
  return 0;
}
