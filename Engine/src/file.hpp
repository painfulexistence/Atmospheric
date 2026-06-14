#pragma once
#include "globals.hpp"

class File
{
private:
    std::string _filename;
    std::optional<std::string> _cached;

public:
    File(const std::string&);

    std::string GetContent() const;
};