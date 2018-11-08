// dump.hpp

#ifndef PYZOR_DUMP_HPP
#define PYZOR_DUMP_HPP

#include <boost/cstdint.hpp>
#include "hash.hpp"

namespace pyzor {

   struct dump_header
   {
      public:
         
         boost::uint32_t version;
   };

   struct dump_record_v1
   {
      public:
         
         hash signature;
         boost::uint32_t report_count;
         boost::uint32_t report_entered;
         boost::uint32_t report_updated;
         boost::uint32_t whitelist_count;
         boost::uint32_t whitelist_entered;
         boost::uint32_t whitelist_updated;
   };

   struct dump_record_v2
   {
      public:
         
         hash signature;
         boost::uint32_t entered;
         boost::uint32_t updated;
         boost::uint32_t report_count;
         boost::uint32_t report_entered;
         boost::uint32_t report_updated;
         boost::uint32_t whitelist_count;
         boost::uint32_t whitelist_entered;
         boost::uint32_t whitelist_updated;
   };

}

#endif // PYZOR_DUMP_HPP
