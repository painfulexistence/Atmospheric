#pragma once
#include "globals.hpp"
#include "message.hpp"

class MessageBus;

// Should only be used as an interface(pure class)
class Messagable
{
public:
    Messagable();
    void ReceiveMessage(Message msg);
protected:
    void ConnectBus(MessageBus* mb);
    void SendMessage(Message msg);
    void SendImmediateMessage(Message msg);
    virtual void OnMessage(Message msg) = 0;
private:
    MessageBus* messageBus;
};