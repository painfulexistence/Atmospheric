#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define TRACY_CALLSTACK 1// Optional: Enable call stack capture for more detailed profiling
#else
#define ZoneScoped
#define ZoneScopedN(name)
#define FrameMark
#define TracyNoop
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "fmt/format.h"

using Vector3 = glm::vec3;
