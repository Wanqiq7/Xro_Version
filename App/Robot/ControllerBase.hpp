#pragma once

#include "app_framework.hpp"

namespace App {

class ControllerBase : public LibXR::Application {
 public:
  ControllerBase(LibXR::ApplicationManager& appmgr, bool auto_register = true);
  ~ControllerBase() override = default;

 protected:
  LibXR::ApplicationManager& appmgr_;
};

}  // namespace App
