// update.hpp

#ifndef PYZOR_UPDATE_HPP
#define PYZOR_UPDATE_HPP

#include <deque>
#include <iostream>

#include <boost/cstdint.hpp>
#include <boost/noncopyable.hpp>

#include "hash.hpp"

namespace pyzor {
   
   struct update {
      public:
         enum update_type { erase, report, whitelist  };
      public:
         update();
         update(hash const& hash, update_type type);
      public:
         pyzor::hash const& ghash() const;
         void ghash(hash const& hash);
         update_type type() const;
         void type(update_type type);
         boost::uint32_t time() const;
         void time(boost::uint32_t time);
      public:
         hash hash_;
         update_type type_;
         boost::uint32_t time_;
   };

   typedef std::deque<update> update_queue;

   std::ostream& operator<<(std::ostream& stream, update::update_type const& t);
   std::ostream& operator<<(std::ostream& stream, update const& u);
}

#endif // PYZOR_UPDATE_HPP
