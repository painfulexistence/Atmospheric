#pragma once
#include "globals.hpp"
#include "message.hpp"

class Application;
class Messagable;
class MessageBus
{
public:
    MessageBus(Application* supervisor);
    ~MessageBus();
    int Register(Messagable* receiver);
    void PostMessage(Message msg);
    void PostImmediateMessage(Message msg);
    void Process();
private:
    const int MAX_PROCESSING_NUM_MSGS = 20;
    Application* _supervisor;
    std::list<Messagable*> _receivers;
    std::queue<Message> _messages;
    void OnMessageSent(Message msg);
};