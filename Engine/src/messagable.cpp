#include "messagable.hpp"
#include "message_bus.hpp"

Messagable::Messagable()
{

}

void Messagable::ConnectBus(MessageBus* mb)
{
    this->messageBus = mb;
    this->messageBus->Register(this);
}

void Messagable::SendMessage(Message msg)
{
    this->messageBus->PostMessage(msg);
}

void Messagable::SendImmediateMessage(Message msg)
{
    this->messageBus->PostImmediateMessage(msg);
}

void Messagable::ReceiveMessage(Message msg)
{
    OnMessage(msg);
}

void Messagable::OnMessage(Message msg)
{
    switch(msg.type)
    {
        default:
            break;
    }
}