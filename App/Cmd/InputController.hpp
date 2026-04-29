#pragma once

#include <cstdint>

#include "../../Modules/MasterMachine/MasterMachine.hpp"
#include "../../Modules/DT7/DT7.hpp"
#include "../../Modules/VT13/VT13.hpp"
#include "../Config/BridgeConfig.hpp"
#include "../Robot/ControllerBase.hpp"
#include "OperatorInputSnapshot.hpp"
#include "RemoteInputMapper.hpp"

namespace App {

class InputController : public ControllerBase {
 public:
  InputController(LibXR::ApplicationManager& appmgr,
                  OperatorInputSnapshot& operator_input,
                  DT7& primary_remote_control,
                  VT13& secondary_remote_control,
                  MasterMachine& master_machine);

  void OnMonitor() override;
  void Update();

 private:
  enum class InputSelection : std::uint8_t {
    kNone = 0,
    kPrimaryDT7,
    kSecondaryVT13,
  };

  bool MasterMachineFieldEnabled(
      Config::MasterMachineInputField field) const;
  void PullInputTopicData();
  InputSelection SelectActiveInput() const;
  void ResetInactiveInputLatches(InputSelection selection);
  void ResetMasterMachineOverrideLatch() const;
  bool ShouldUseMasterMachineOverride() const;
  void ApplySelectedInput(InputSelection selection,
                          OperatorInputSnapshot& input) const;
  void ApplyMasterMachineOverride(OperatorInputSnapshot& input) const;

  OperatorInputSnapshot& operator_input_;
  DT7& primary_remote_control_;
  VT13& secondary_remote_control_;
  MasterMachine& master_machine_;

  DT7State primary_remote_state_{};
  VT13State secondary_remote_state_{};
  MasterMachineState master_machine_state_{};
  mutable InputLatchState primary_latch_{};
  mutable InputLatchState secondary_latch_{};
  mutable bool master_machine_fire_request_latched_ = false;
  mutable std::uint32_t master_machine_shot_request_seq_ = 0;

  LibXR::Topic::ASyncSubscriber<DT7State> primary_remote_subscriber_;
  LibXR::Topic::ASyncSubscriber<VT13State> secondary_remote_subscriber_;
  LibXR::Topic::ASyncSubscriber<MasterMachineState> master_machine_subscriber_;
};

}  // namespace App
