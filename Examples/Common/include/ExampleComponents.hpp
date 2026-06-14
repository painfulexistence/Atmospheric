#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ExampleComponents — a small library of reusable, header-only gameplay
// components shared across the Atmospheric example projects.
//
// Everything lives in namespace `axex`. Each component derives from the engine's
// Component class and is attached with GameObject::AddComponent<T>(...), so the
// engine ticks it automatically. Grouped by concern:
//
//   Motion.hpp            RotatorComponent, OscillatorComponent,
//                         LinearMoverComponent, LifetimeComponent
//   Visual.hpp            SpritePulseComponent, ColorRestoreComponent
//   Labels.hpp            WorldLabelComponent, ScreenLabelComponent
//   CameraController.hpp  FlyCameraComponent
//   Parallax.hpp          ParallaxLayerComponent
// ─────────────────────────────────────────────────────────────────────────────

#include "ExampleComponents/CameraController.hpp"
#include "ExampleComponents/Labels.hpp"
#include "ExampleComponents/Motion.hpp"
#include "ExampleComponents/Parallax.hpp"
#include "ExampleComponents/Visual.hpp"
