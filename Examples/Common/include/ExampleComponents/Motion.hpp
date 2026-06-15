#pragma once
// RotatorComponent, OscillatorComponent, LinearMoverComponent, LifetimeComponent
// are now built into the engine. Including the engine header is sufficient.
#include "Atmospheric/motion_components.hpp"

// Re-export into the axex namespace so any code that previously wrote
// `axex::RotatorComponent` continues to compile without changes.
namespace axex {
using ::RotatorComponent;
using ::OscillatorComponent;
using ::LinearMoverComponent;
using ::LifetimeComponent;
}// namespace axex
