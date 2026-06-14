# ExampleComponents

A tiny, header-only library of **reusable gameplay components** shared across the
Atmospheric examples. It exists to keep the examples idiomatic: instead of
piling per-object behaviour into `Application::OnUpdate`, each behaviour is a
`Component` you attach to a `GameObject`, and the engine ticks it for you.

```cpp
#include "ExampleComponents.hpp"
using namespace axex;

cube->AddComponent<RotatorComponent>(glm::vec3(0.0f, 0.5f, 1.0f));
```

All components live in namespace `axex` and depend only on the public engine API.

## Catalogue

| Component                | Group                  | What it does |
|--------------------------|------------------------|--------------|
| `RotatorComponent`       | `Motion.hpp`           | Spins the object at a constant angular velocity. |
| `OscillatorComponent`    | `Motion.hpp`           | Sinusoidal position offset around an anchor point. |
| `LinearMoverComponent`   | `Motion.hpp`           | Moves the object at a constant velocity. |
| `LifetimeComponent`      | `Motion.hpp`           | Deactivates the object after N seconds. |
| `SpritePulseComponent`   | `Visual.hpp`           | Pulses a `SpriteComponent`'s alpha. |
| `ColorRestoreComponent`  | `Visual.hpp`           | Eases a `ShapeRendererComponent` colour back to its base (hit-flash recovery). |
| `WorldLabelComponent`    | `Labels.hpp`           | 3D text label that follows the object. |
| `ScreenLabelComponent`   | `Labels.hpp`           | Fixed screen-space HUD text. |
| `FlyCameraComponent`     | `CameraController.hpp` | WASD/RF/IJKL world-axis fly camera. |
| `ParallaxLayerComponent` | `Parallax.hpp`         | One seamless, infinitely-scrolling background layer. |

## Using it from CMake

```cmake
target_link_libraries(MyExample PRIVATE ExampleComponents)
```

> These components were grown out of the examples. If any of them prove broadly
> useful they are good candidates to graduate into the engine proper
> (`Engine/include/Atmospheric/`).
