set pagination off
set confirm off
set print pretty on
set breakpoint pending on

# 使用当前 Debug ELF。注意：只有在重新构建成功后，它才代表新鲜固件。
file build/Debug/basic_framework.elf

# 默认连接本地 J-Link GDB Server。
target extended-remote localhost:2331
monitor halt
monitor reset
monitor reset halt

echo \n== Pitch bring-up breakpoints ==\n
break StartDefaultTask
break app_main
break App::AppRuntime::AppRuntime
break App::PitchDMMotorBridge::Update
break App::GimbalRole::UpdateGimbalState
rbreak DMMotor::Enable
rbreak DMMotor::OnMonitor
rbreak DMMotor::UpdateFeedback

echo \n== Suggested observations ==\n
echo 1) 在 App::PitchDMMotorBridge::Update 处执行: p input / p state_ / p motor_.feedback_ / p motor_.command_\n
echo 2) 在 DMMotor::UpdateFeedback 处执行: p feedback_\n
echo 3) 在 App::GimbalRole::UpdateGimbalState 处执行: p pitch_bridge_state / p gimbal_state_ / p robot_ready\n
echo 4) 若需查看继续运行后的状态，使用: continue\n

monitor go
