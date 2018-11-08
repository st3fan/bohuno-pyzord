// update.cpp

#include <netinet/in.h>
#include "update.hpp"

namespace pyzor {

   update::update()
   {
   }

   update::update(hash const& hash, update_type type)
   {
      this->ghash(hash);
      this->type(type);
      this->time(::time(NULL));
   }
   
   hash const& update::ghash() const
   {
      return hash_;
   }

   void update::ghash(hash const& hash)
   {
      hash_ = hash;
   }

   update::update_type update::type() const
   {
      return (update::update_type) ntohl((boost::uint32_t) type_);
   }

   void update::type(update::update_type type)
   {
      type_ = (update::update_type) htonl((boost::uint32_t) type);
   }

   boost::uint32_t update::time() const
   {
      return ntohl(time_);
   }

   void update::time(boost::uint32_t time)
   {
      time_ = htonl(time);
   }

   std::ostream& operator<<(std::ostream& stream, update::update_type const& t)
   {
      switch (ntohl(t)) {
         case update::erase:
            stream << "erase";
            break;
         case update::report:            
            stream << "report";
            break;
         case update::whitelist:
            stream << "whitelist";
            break;
      }
      return stream;
   }

   std::ostream& operator<<(std::ostream& stream, update const& u)
   {
      stream << u.type() << " " << u.ghash() << " " << u.time();
      return stream;
   }

}
