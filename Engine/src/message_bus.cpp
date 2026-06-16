#include "message_bus.hpp"
#include "messagable.hpp"
#include "application.hpp"

MessageBus::MessageBus(Application* supervisor)
{
    this->_supervisor = supervisor;
}

MessageBus::~MessageBus()
{

}

int MessageBus::Register(Messagable* receiver)
{
    int id = _receivers.size();
    this->_receivers.push_back(receiver);
    return id;
}

void MessageBus::PostMessage(Message msg)
{
    _messages.push(msg);
}

void MessageBus::PostImmediateMessage(Message msg)
{
    if (msg.type == MessageType::ON_QUIT)
    {
        _supervisor->Quit();
        return;
    }
    for (auto m : _receivers)
    {
        m->ReceiveMessage(msg);
    }
}

void MessageBus::Process()
{
    int processedCount = 0;
    while (!this->_messages.empty() && processedCount < MAX_PROCESSING_NUM_MSGS)
    {
        auto msg = this->_messages.front();
        PostImmediateMessage(msg);
        OnMessageSent(msg);

        processedCount++;
        this->_messages.pop();
    }
}

void MessageBus::OnMessageSent(Message msg)
{
    PostImmediateMessage(Message(MessageType::ON_MESSAGE_SENT));
}