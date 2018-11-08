// hash.hpp

#ifndef PYZOR_HASH_HPP
#define PYZOR_HASH_HPP

#include <iostream>
#include <boost/cstdint.hpp>

namespace pyzor {

   struct hash {
      public:
         hash();
         hash(boost::uint8_t data[20]);
         hash(std::string const& hex);
      public:
         boost::uint8_t data_[20];
   };

   std::ostream& operator<<(std::ostream& stream, hash const& t);

}

#endif // PYZOR_HASH_HPP
