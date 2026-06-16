#include "physics_debug_drawer.hpp"
#include "graphics_server.hpp"

void PhysicsDebugDrawer::reportErrorWarning(const char* text)
{
    fmt::print("Bullet Warning: {}\n", text);
}

void PhysicsDebugDrawer::drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
{
    // TODO: check if singleton exists?
    GraphicsServer::Get()->PushDebugLine(
        { glm::vec3(from.getX(), from.getY(), from.getZ()), glm::vec3(color.getX(), color.getY(), color.getZ()) },
        { glm::vec3(to.getX(), to.getY(), to.getZ()), glm::vec3(color.getX(), color.getY(), color.getZ()) }
    );
}

void PhysicsDebugDrawer::drawContactPoint(const btVector3& point, const btVector3& normal, btScalar distance, int lifeTime, const btVector3& color)
{
    // TODO: check if singleton exists?
    GraphicsServer::Get()->PushDebugLine(
        { glm::vec3(point.getX(), point.getY(), point.getZ()), glm::vec3(color.getX(), color.getY(), color.getZ()) },
        { glm::vec3(point.getX() + distance * normal.getX(), point.getY() + distance * normal.getY(), point.getZ() + distance * normal.getZ()), glm::vec3(color.getX(), color.getY(), color.getZ()) }
    );
}

void PhysicsDebugDrawer::draw3dText(const btVector3& location, const char* textString)
{
    // TODO
}