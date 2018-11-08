// base64.hpp

#ifndef BASE64_HPP
#define BASE64_HPP

#include <string>

namespace base64 {
	
   std::string encode(std::string const& data);
   std::string decode(std::string const& data);
	
}

#endif
