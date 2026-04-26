#pragma once

#include <cstdint>
#include <memory>

#include "../../Modules/MasterMachine/MasterMachine.hpp"
#include "../../Modules/RemoteControl/DT7.hpp"
#include "../../Modules/RemoteControl/VT13.hpp"
#include "../Config/BridgeConfig.hpp"
#include "../Robot/ControllerBase.hpp"
#include "OperatorInputSnapshot.hpp"

namespace App {

class InputController : public ControllerBase {
 public:
  InputController(LibXR::ApplicationManager& appmgr,
                  OperatorInputSnapshot& operator_input,
                  DT7& primary_remote_control,
                  DT7* secondary_remote_control, VT13& vt13,
                  MasterMachine& master_machine);

  void OnMonitor() override;
  void Update();

 private:
  enum class InputSelection : std::uint8_t {
    kNone = 0,
    kPrimary,
    kSecondary,
    kVT13,
  };

  bool MasterMachineFieldEnabled(
      Config::MasterMachineInputField field) const;
  void PullInputTopicData();
  InputSelection SelectActiveInput() const;
  bool ShouldUseMasterMachineOverride() const;
  void ApplySelectedInput(InputSelection selection,
                          OperatorInputSnapshot& input) const;
  void ApplyMasterMachineOverride(OperatorInputSnapshot& input) const;

  OperatorInputSnapshot& operator_input_;
  DT7& primary_remote_control_;
  DT7* secondary_remote_control_ = nullptr;
  VT13& vt13_;
  MasterMachine& master_machine_;

  DT7State primary_remote_state_{};
  DT7State secondary_remote_state_{};
  VT13State vt13_state_{};
  MasterMachineState master_machine_state_{};

  LibXR::Topic::ASyncSubscriber<DT7State> primary_remote_subscriber_;
  std::unique_ptr<LibXR::Topic::ASyncSubscriber<DT7State>>
      secondary_remote_subscriber_;
  LibXR::Topic::ASyncSubscriber<VT13State> vt13_subscriber_;
  LibXR::Topic::ASyncSubscriber<MasterMachineState> master_machine_subscriber_;
};

}  // namespace App
