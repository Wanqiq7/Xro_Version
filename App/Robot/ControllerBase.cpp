#include "ControllerBase.hpp"

namespace App {

ControllerBase::ControllerBase(LibXR::ApplicationManager& appmgr,
                               bool auto_register)
    : appmgr_(appmgr) {
  if (auto_register) {
    appmgr_.Register(*this);
  }
}

}  // namespace App
