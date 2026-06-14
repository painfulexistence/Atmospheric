#include "../lua_application.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

void BindCoreTypes(sol::state& lua)
{
    // ===== vec2 =====
    lua.new_usertype<glm::vec2>("vec2",
        sol::constructors<
            glm::vec2(),
            glm::vec2(float),
            glm::vec2(float, float)
        >(),

        "x", &glm::vec2::x,
        "y", &glm::vec2::y,

        // Operators
        sol::meta_function::addition, [](const glm::vec2& a, const glm::vec2& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec2& a, const glm::vec2& b) { return a - b; },
        sol::meta_function::multiplication, sol::overload(
            [](const glm::vec2& a, float s) { return a * s; },
            [](float s, const glm::vec2& a) { return s * a; },
            [](const glm::vec2& a, const glm::vec2& b) { return a * b; }
        ),
        sol::meta_function::division, sol::overload(
            [](const glm::vec2& a, float s) { return a / s; },
            [](const glm::vec2& a, const glm::vec2& b) { return a / b; }
        ),
        sol::meta_function::unary_minus, [](const glm::vec2& a) { return -a; },
        sol::meta_function::to_string, [](const glm::vec2& v) {
            return fmt::format("vec2({}, {})", v.x, v.y);
        },

        // Methods
        "length", [](const glm::vec2& v) { return glm::length(v); },
        "normalize", [](const glm::vec2& v) { return glm::normalize(v); },
        "dot", [](const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); }
    );

    // ===== vec3 =====
    lua.new_usertype<glm::vec3>("vec3",
        sol::constructors<
            glm::vec3(),
            glm::vec3(float),
            glm::vec3(float, float, float)
        >(),

        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,

        // Operators
        sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication, sol::overload(
            [](const glm::vec3& a, float s) { return a * s; },
            [](float s, const glm::vec3& a) { return s * a; },
            [](const glm::vec3& a, const glm::vec3& b) { return a * b; }
        ),
        sol::meta_function::division, sol::overload(
            [](const glm::vec3& a, float s) { return a / s; },
            [](const glm::vec3& a, const glm::vec3& b) { return a / b; }
        ),
        sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
        sol::meta_function::to_string, [](const glm::vec3& v) {
            return fmt::format("vec3({}, {}, {})", v.x, v.y, v.z);
        },

        // Methods
        "length", [](const glm::vec3& v) { return glm::length(v); },
        "normalize", [](const glm::vec3& v) { return glm::normalize(v); },
        "dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
        "cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); }
    );

    // ===== vec4 =====
    lua.new_usertype<glm::vec4>("vec4",
        sol::constructors<
            glm::vec4(),
            glm::vec4(float),
            glm::vec4(float, float, float, float),
            glm::vec4(glm::vec3, float)
        >(),

        "x", &glm::vec4::x,
        "y", &glm::vec4::y,
        "z", &glm::vec4::z,
        "w", &glm::vec4::w,

        sol::meta_function::to_string, [](const glm::vec4& v) {
            return fmt::format("vec4({}, {}, {}, {})", v.x, v.y, v.z, v.w);
        }
    );

    // ===== quat =====
    lua.new_usertype<glm::quat>("quat",
        sol::constructors<
            glm::quat(),
            glm::quat(float, float, float, float),
            glm::quat(glm::vec3)  // From euler angles
        >(),

        "x", &glm::quat::x,
        "y", &glm::quat::y,
        "z", &glm::quat::z,
        "w", &glm::quat::w,

        sol::meta_function::multiplication, [](const glm::quat& a, const glm::quat& b) { return a * b; },
        sol::meta_function::to_string, [](const glm::quat& q) {
            return fmt::format("quat({}, {}, {}, {})", q.w, q.x, q.y, q.z);
        },

        // Static constructors
        "fromEuler", [](float x, float y, float z) { return glm::quat(glm::vec3(x, y, z)); },
        "fromAxisAngle", [](const glm::vec3& axis, float angle) { return glm::angleAxis(angle, axis); }
    );

    // ===== Global math functions =====
    sol::table atmos = lua["atmos"];
    sol::table math = atmos.create("math");

    math["lerp"] = sol::overload(
        [](float a, float b, float t) { return glm::mix(a, b, t); },
        [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); }
    );
    math["clamp"] = sol::overload(
        [](float v, float min, float max) { return glm::clamp(v, min, max); },
        [](const glm::vec3& v, const glm::vec3& min, const glm::vec3& max) { return glm::clamp(v, min, max); }
    );
    math["radians"] = [](float deg) { return glm::radians(deg); };
    math["degrees"] = [](float rad) { return glm::degrees(rad); };
}
