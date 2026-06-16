#pragma once
#include "globals.hpp"

enum MessageType
{
    ON_MESSAGE_SENT,
    ON_KEY_PRESSED,
    ON_KEY_RELEASED,
    ON_CURSOR_MOVED,
    ON_QUIT,
    DRAW_CALL
};

class Message
{
public:
    Message(MessageType type);
    MessageType type;
};