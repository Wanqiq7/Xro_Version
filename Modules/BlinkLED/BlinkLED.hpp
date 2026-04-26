#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 控制 LED 闪烁的简单模块 / A simple module to control LED blinking
constructor_args:
  - blink_cycle: 250
template_args: []
required_hardware: led/LED/led1/LED1
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "gpio.hpp"
#include "libxr_def.hpp"
#include "timer.hpp"

class BlinkLED : public LibXR::Application {
 public:
  BlinkLED(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
           uint32_t blink_cycle)
      : led_(hw.template FindOrExit<LibXR::GPIO>(
            {"led", "LED", "led1", "LED1"})),
        timer_handle_(
            LibXR::Timer::CreateTask(BlinkTaskFun, this, blink_cycle)) {
    UNUSED(app);

    LibXR::Timer::Add(timer_handle_);
    LibXR::Timer::Start(timer_handle_);

    auto error_callback = LibXR::Assert::Callback::Create(
        [](bool in_isr, BlinkLED* led, const char* file, uint32_t line) {
          UNUSED(file);
          UNUSED(line);

          LibXR::Timer::Stop(led->timer_handle_);

          if (!in_isr) {
            while (true) {
              led->led_->Write(false);
              LibXR::Thread::Sleep(125);
              led->led_->Write(true);
              LibXR::Thread::Sleep(125);
              led->led_->Write(false);
              LibXR::Thread::Sleep(500);
              led->led_->Write(true);
              LibXR::Thread::Sleep(500);
            }
          }
        },
        this);

    LibXR::Assert::RegisterFatalErrorCallback(error_callback);
  }

  static void BlinkTaskFun(BlinkLED* blink) {
    blink->flag_ = !blink->flag_;
    blink->led_->Write(blink->flag_);
  }

  void OnMonitor() override {}

 private:
  bool flag_ = false;
  LibXR::GPIO* led_;
  LibXR::Timer::TimerHandle timer_handle_;
};
