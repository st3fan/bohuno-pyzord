// hash.cpp

#include "hash.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace pyzor {

   hash::hash()
   {
      std::memset(data_, 0, sizeof(data_));
   }

   hash::hash(boost::uint8_t data[20])
   {
      std::memcpy(data_, data, sizeof(data_));
   }
   
   hash::hash(std::string const& hex)
   {
      if (hex.length() == 40)
      {
         for (int i = 0; i < 20; i++) {
            char s[3];
            s[0] = hex[(i*2)];
            s[1] = hex[(i*2)+1];
            s[2] = 0x00;
            data_[i] = std::strtol(s, NULL, 16);
         }
      }
   }

   std::ostream& operator<<(std::ostream& stream, hash const& h)
   {
      std::ios_base::fmtflags flags = stream.flags();

      for (size_t i = 0; i < sizeof(h.data_); i++) {
         stream << std::hex << std::setw(2) << std::setfill('0') << (int) h.data_[i];
      }

      stream.flags(flags);

      return stream;
   }   

}
