#include "../lua_application.hpp"
#include "Atmospheric/physics_server.hpp"
#include "Atmospheric/rigidbody_component.hpp"

void BindPhysicsAPI(sol::state& lua, LuaApplication* app) {
    sol::table atmos = lua["atmos"];
    sol::table physics = atmos.create("physics");

    // ===== Raycast =====
    physics["raycast"] = [&lua](const glm::vec3& from, const glm::vec3& to) -> sol::object {
        RaycastHit hit;
        if (PhysicsServer::Get()->Raycast(from, to, hit)) {
            sol::table result = lua.create_table();
            result["point"] = hit.point;
            result["normal"] = hit.normal;
            result["distance"] = hit.hitDistance;
            result["gameObject"] = hit.gameObject;
            return result;
        }
        return sol::lua_nil;
    };

    // Set global gravity
    physics["setGravity"] = [](const glm::vec3& gravity) { PhysicsServer::Get()->SetGravity(gravity); };

    // ===== RigidbodyComponent usertype =====
    lua.new_usertype<RigidbodyComponent>(
      "RigidbodyComponent",
      sol::no_constructor,

      // Mass
      "mass",
      sol::property(&RigidbodyComponent::GetMass, &RigidbodyComponent::SetMass),

      // Forces
      "addForce",
      &RigidbodyComponent::AddForce,
      "addForceAtPosition",
      &RigidbodyComponent::AddForceAtPosition,
      "addImpulse",
      &RigidbodyComponent::AddImpulse,
      "addImpulseAtPosition",
      &RigidbodyComponent::AddImpulseAtPosition,
      "addTorque",
      &RigidbodyComponent::AddTorque,
      "addTorqueImpulse",
      &RigidbodyComponent::AddTorqueImpulse,

      // Velocity
      "linearVelocity",
      sol::property(&RigidbodyComponent::GetLinearVelocity, &RigidbodyComponent::SetLinearVelocity),
      "angularVelocity",
      sol::property(&RigidbodyComponent::GetAngularVelocity, &RigidbodyComponent::SetAngularVelocity),

      // Factors
      "linearFactor",
      sol::property(&RigidbodyComponent::GetLinearFactor, &RigidbodyComponent::SetLinearFactor),
      "angularFactor",
      sol::property(&RigidbodyComponent::GetAngularFactor, &RigidbodyComponent::SetAngularFactor),

      // Gravity (per-body override)
      "setGravity",
      &RigidbodyComponent::SetGravity,

      // Sleep state
      "wakeUp",
      &RigidbodyComponent::WakeUp,
      "sleep",
      &RigidbodyComponent::Sleep,

      // Component base
      "enabled",
      &RigidbodyComponent::enabled,
      "gameObject",
      sol::readonly(&RigidbodyComponent::gameObject)
    );

    // ===== GameObject rigidbody access =====
    // Add getRigidbody() to GameObject (extend existing usertype)
    sol::usertype<GameObject> goType = lua["GameObject"];
    goType["getRigidbody"] = [](GameObject* go) -> RigidbodyComponent* {
        return go->GetComponent<RigidbodyComponent>();
    };
}
