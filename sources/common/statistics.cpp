
#include "statistics.hpp"

#include <iostream>

namespace pyzor {

   statistics_ring::statistics_ring(int seconds)
      : buckets_(seconds), current_(buckets_.begin()), total_(0)
   {
      current_->time = time(NULL);
   }
   
   void statistics_ring::report()
   {
      total_++;
      advance();
      current_->count++;
   }
   
   boost::uint64_t statistics_ring::average()
   {
      advance();
      boost::uint64_t total = 0;
      for (size_t i = 0; i < buckets_.size(); i++) {
         total += buckets_[i].count;
      }
      return (total / buckets_.size());
   }

   boost::uint64_t statistics_ring::total()
   {
      return total_;
   }

   void statistics_ring::advance()
   {
      time_t now = time(NULL);

      if (current_->time != now) {
         for (time_t t = current_->time + 1; t <= now; t++) {
            current_++;
            if (current_ == buckets_.end()) {
               current_ = buckets_.begin();
            }
            current_->count = 0;
            current_->time = t;
         }
      }
   }

}
