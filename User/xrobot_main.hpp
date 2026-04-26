#include "../App/Robot/AppRuntime.hpp"
#include "libxr.hpp"

static void XRobotMain(LibXR::HardwareContainer &hw) {
  static LibXR::ApplicationManager application_manager;
  static App::AppRuntime runtime(hw, application_manager);
  App::g_app_runtime = &runtime;
  static_cast<void>(runtime);

  while (true) {
    application_manager.MonitorAll();
    LibXR::Thread::Sleep(1);
  }
}
