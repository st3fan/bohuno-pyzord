
#ifndef STATISTICS_HPP
#define STATISTICS_HPP

#include <time.h>
#include <vector>
#include <boost/cstdint.hpp>

namespace pyzor  {

   class statistics_ring
   {
      public:
         
         statistics_ring(int seconds = 300);
         
      public:

         void report();
         boost::uint64_t average();
         boost::uint64_t total();

      private:

         struct bucket
         {
            public:
               
               bucket()
                  : time(0), count(0)
               {
               }
               
               bucket(bucket const& other)
                  : time(other.time), count(other.count)
               {
               }

            public:

               void reset()
               {
                  time = 0;
                  count = 0;
               }

            public:

               time_t time;
               boost::uint64_t count;
         };

      private:

         void advance();
         
      private:

         std::vector<bucket> buckets_;
         std::vector<bucket>::iterator current_;
         boost::uint64_t total_;
   };
   
}

#endif
