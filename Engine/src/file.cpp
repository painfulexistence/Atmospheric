#include "file.hpp"
#include "file_system.hpp"

File::File(const std::string& filename) : _filename(filename) {
}

std::string File::GetContent() const {
    if (this->_cached.has_value()) return *_cached;

    auto bytes = FileSystem::Get().ReadSync(_filename);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
