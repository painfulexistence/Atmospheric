#pragma once
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <list>
#include <map>
#include <queue>
#include <stack>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <optional>
#include <utility>
#include <stdexcept>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define TRACY_CALLSTACK 1 // Optional: Enable call stack capture for more detailed profiling
#else
#define ZoneScoped
#define ZoneScopedN(name)
#define FrameMark
#define TracyNoop
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "fmt/core.h"

using Vector3 = glm::vec3;