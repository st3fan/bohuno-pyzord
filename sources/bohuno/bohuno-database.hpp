
#ifndef BOHUNO_DATABASE_HPP
#define BOHUNO_DATABASE_HPP

#include <db.h>

#include <boost/function.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>

#include "hash.hpp"
#include "record.hpp"

namespace bohuno {

   ///

   class database : boost::noncopyable
   {
      public:

         typedef boost::function<void(size_t n)> import_progress_callback;
         
      public:

         database(boost::filesystem::path const& home);
         ~database();

      public:

         void setup();
         void teardown();
         
      public:

         bool empty();
         bool lookup(pyzor::hash const& hash, pyzor::record& record);
         bool lookup_last(pyzor::hash& hash, pyzor::record& record);
         void insert(pyzor::hash const& hash, pyzor::record const& record);
         int import(boost::iostreams::filtering_istream& in, import_progress_callback callback = 0L);
         void checkpoint();

      private:
         
         boost::filesystem::path home_;
         DB_ENV* env_;
         DB* db_;
         DB* index_;         
   };

} // namespace bohuno

#endif // BOHUNO_DATABASE_HPP
