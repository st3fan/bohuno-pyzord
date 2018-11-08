
#ifndef RECORD_HPP
#define RECORD_HPP

#include <time.h>

#include <vector>
#include <boost/cstdint.hpp>
#include <boost/shared_ptr.hpp>

namespace pyzor {

   struct record
   {
      public:

         record();
         record(record const& other);

      public:

         void report(boost::uint32_t time = 0);
         void whitelist(boost::uint32_t time = 0);
         void reset(boost::uint32_t time = 0);
         
      public:

         boost::uint32_t entered();
         boost::uint32_t updated();         
         boost::uint32_t report_count();
         boost::uint32_t report_entered();
         boost::uint32_t report_updated();
         boost::uint32_t whitelist_count();
         boost::uint32_t whitelist_entered();
         boost::uint32_t whitelist_updated();

         void entered(boost::uint32_t v);
         void updated(boost::uint32_t v);
         void report_count(boost::uint32_t v);
         void report_entered(boost::uint32_t v);
         void report_updated(boost::uint32_t v);
         void whitelist_count(boost::uint32_t v);
         void whitelist_entered(boost::uint32_t v);
         void whitelist_updated(boost::uint32_t v);

      public:

         boost::uint32_t entered_;
         boost::uint32_t updated_;
         boost::uint32_t report_count_;
         boost::uint32_t report_entered_;
         boost::uint32_t report_updated_;
         boost::uint32_t whitelist_count_;
         boost::uint32_t whitelist_entered_;
         boost::uint32_t whitelist_updated_;
   };

   typedef boost::shared_ptr<record> record_ptr;

   typedef std::vector<record> record_vector;
   typedef boost::shared_ptr<record_vector> record_vector_ptr;

}

#endif
