#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::pd_console {

// The Power Distribution console: a header tile (sensor.pd_screen_header) and a battery tile per device
// (sensor.pd_screen_batteries). Both read flat attributes of a template sensor, see README.md for the contract.
class PdConsole : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace esphome::pd_console
