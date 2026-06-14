#include "file.hpp"
#include <fstream>

File::File(const std::string& filename) : _filename(filename) 
{

}

std::string File::GetContent() const 
{
    if (this->_cached.has_value())
        return *_cached;
    
    std::ifstream ifs(_filename.c_str());
    std::string content(
        (std::istreambuf_iterator<char>(ifs)), //start of stream iterator
        (std::istreambuf_iterator<char>()) //end of stream iterator
    );
    return content;
}
