
#include <arpa/inet.h>

#include "record.hpp"

namespace pyzor {

   record::record()
   {
      entered_ = 0;
      updated_ = 0;
      
      whitelist_count_ = 0;
      whitelist_entered_ = 0;
      whitelist_updated_ = 0;
      report_count_ = 0;
      report_entered_ = 0;
      report_updated_ = 0;
   }
         
   record::record(record const& other)
   {
      entered_ = other.entered_;
      updated_ = other.updated_;

      whitelist_count_ = other.whitelist_count_;
      whitelist_entered_ = other.whitelist_entered_;
      whitelist_updated_ = other.whitelist_updated_;
      report_count_ = other.report_count_;
      report_entered_ = other.report_entered_;
      report_updated_ = other.report_updated_;
   }

   boost::uint32_t record::entered()
   {
      return ntohl(entered_);
   }

   boost::uint32_t record::updated()
   {
      return ntohl(updated_);
   }
               
   void record::report(boost::uint32_t t)
   {
      report_count(report_count() + 1);
      
      if (t == 0) {
         t = time(NULL);
         if (report_entered() == 0) {
            report_entered(t);
         }      
         report_updated(t);
      } else {
         if (report_updated() < t) {
            report_updated(t);
            if (report_entered() == 0) {
               report_entered(report_updated());
            }
         }
      }

      if (entered() == 0) {
         entered(t);
      }
      
      if (updated() < t) {
         updated(t);
      }
   }

   void record::whitelist(boost::uint32_t t)
   {
      whitelist_count(whitelist_count() + 1);

      if (t == 0) {
         t = time(NULL);
         if (whitelist_entered() == 0) {
            whitelist_entered(t);
         }
         whitelist_updated(t);
      } else {
         if (whitelist_updated() < t) {
            whitelist_updated(t);
            if (whitelist_entered() == 0) {
               whitelist_entered(whitelist_updated());
            }
         }
      }

      if (entered() == 0) {
         entered(t);
      }

      if (updated() < t) {
         updated(t);
      }
   }

   void record::reset(boost::uint32_t t)
   {
      whitelist_count(0);
      report_count(0);
      
      if (t == 0) {
         t = time(NULL);
      }

      report_updated(0);
      whitelist_updated(0);
      updated(t);
   }

   boost::uint32_t record::report_count()
   {
      return ntohl(report_count_);
   }

   boost::uint32_t record::report_entered()
   {
      return ntohl(report_entered_);
   }

   boost::uint32_t record::report_updated()
   {
      return ntohl(report_updated_);
   }

   boost::uint32_t record::whitelist_count()
   {
      return ntohl(whitelist_count_);
   }

   boost::uint32_t record::whitelist_entered()
   {
      return ntohl(whitelist_entered_);
   }

   boost::uint32_t record::whitelist_updated()
   {
      return ntohl(whitelist_updated_);
   }

   void record::entered(boost::uint32_t v)
   {
      entered_ = htonl(v);
   }

   void record::updated(boost::uint32_t v)
   {
      updated_ = htonl(v);
   }

   void record::report_count(boost::uint32_t v)
   {
      report_count_ = htonl(v);
   }

   void record::report_entered(boost::uint32_t v)
   {
      report_entered_ = htonl(v);
   }

   void record::report_updated(boost::uint32_t v)
   {
      report_updated_ = htonl(v);
   }

   void record::whitelist_count(boost::uint32_t v)
   {
      whitelist_count_ = htonl(v);
   }

   void record::whitelist_entered(boost::uint32_t v)
   {
      whitelist_entered_ = htonl(v);
   }

   void record::whitelist_updated(boost::uint32_t v)
   {
      whitelist_updated_ = htonl(v);
   }

}
