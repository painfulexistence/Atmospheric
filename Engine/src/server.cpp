#include "server.hpp"
#include "application.hpp"

void Server::Init(Application* app)
{
    _app = app;
    _initialized = true;
}

void Server::Process(float dt)
{
    // Default processing
}

void Server::DrawImGui(float dt)
{

}