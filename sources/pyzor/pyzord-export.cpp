// pyzord-export.cpp

#include <fstream>
#include <iostream>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/timer.hpp>

#include <netinet/in.h>

#include <db.h>

#include "record.hpp"
#include "hash.hpp"

struct pyzord_export_options
{
   public:
      
      pyzord_export_options()
         : home("/var/lib/pyzor"), output("pyzor.dump")
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzord-export [-d database-dir] -f output-file" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "d:f:")) != EOF) {
            switch (c) {
               case 'd':
                  home = optarg;
                  break;
               case 'f':
                  output = optarg;
                  break;
               default:
                  usage();
                  return false;
            }
         }

         if (boost::filesystem::exists(output)) {
            std::cout << "pyzord-export: output file already exists. will not overwrite." << std::endl;
            return false;
         }

         return true;
      }

   public:
      
      boost::filesystem::path home;
      boost::filesystem::path output;
};

// I'm really not happy with a third copy of this code

class database : boost::noncopyable
{
   public:
      
      database(boost::filesystem::path const& home)
         : home_(home), db_home_(home / "db"), env_(NULL), db_(NULL)
      {
         // Check if the directories are there

         if (!boost::filesystem::exists(home_)) {
            throw std::runtime_error("pyzor home does not exist");
         }
         
         if (!boost::filesystem::exists(db_home_)) {
            throw std::runtime_error("pyzor home does not contain a db directory");
         }

         // Create a database environment
      
         int ret = db_env_create(&env_, 0);            
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot create the environment: ") + db_strerror(ret));
         }
      
         // Open the environment
         
         u_int32_t flags =  DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_THREAD;
      
         ret = env_->open(env_, db_home_.string().c_str(), flags, 0);
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot open the environment: ") + db_strerror(ret));
         }
      
         // Create the database
         
         ret = db_create(&db_, env_, 0);
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot create the database: ") + db_strerror(ret));
         }
         
         ret = db_->open(
            db_,
            NULL,
            "signatures.db",
            NULL,
            DB_UNKNOWN,
            DB_RDONLY | DB_THREAD,
            0
         );
      
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot setup the database: ") + db_strerror(ret));
         }
      }         
      
      ~database()
      {
         if (db_ != NULL) {
            int ret = db_->close(db_, 0);
            if (ret != 0) {
               // XXX
            }
            db_ = NULL;
         }
         
         if (env_ != NULL) {
            int ret = env_->close(env_, 0);
            if (ret != 0) {
               // XXX
            }
            env_ = NULL;
         }
      }

   public:

      size_t dump(boost::filesystem::path const& path)
      {
         boost::iostreams::filtering_ostream out;
         out.push(boost::iostreams::gzip_compressor());
         out.push(boost::iostreams::file_sink(path.string(), std::ios::binary));
         
         // Print the header
         
         boost::uint32_t header = htonl(2);
         out.write(reinterpret_cast<char*>(&header), sizeof(header));      
         
         // Find all matching records
         
         DBC *cursor;
         
         DBT key;
         memset(&key, 0, sizeof(DBT));
         
         DBT data;
         memset(&data, 0, sizeof(DBT));
         
         db_->cursor(db_, NULL, &cursor, 0);
      
         size_t n = 0;
         
         int ret = 0;
         while ((ret = cursor->get(cursor, &key, &data, DB_NEXT)) == 0) {
            out.write(reinterpret_cast<char*>(key.data), key.size);
            out.write(reinterpret_cast<char*>(data.data), data.size);
            n++;
            if ((n % 100000) == 0) {
               std::cout << "pyzord-export: exported " << n << " records." << std::endl;
            }
         }

         if ((n % 100000) != 0) {
            std::cout << "pyzord-export: exported " << n << " records." << std::endl;
         }
         
         cursor->close(cursor);      
         
         return n;         
      }

   private:

      boost::filesystem::path home_;
      boost::filesystem::path db_home_;
      
      DB_ENV* env_;
      DB* db_;
};

int main(int argc, char** argv)
{
   int result = 0;

   pyzord_export_options options;
   if (options.parse(argc, argv)) {
      try {
         std::cout << "pyzord-export: exporting " << options.home << " to " << options.output << std::endl;
         database db(options.home);
         boost::timer timer;
         size_t n = db.dump(options.output);
         double elapsed = timer.elapsed();
         std::cout << "pyzord-export: exported " << n << " records in " << elapsed << " seconds (" << (n / elapsed) << " records/second)" << std::endl;
      } catch (std::exception const& e) {
         std::cout << "pyzord-export: could not export database: " << e.what() << std::endl;
         result = 1;
      }
   } else {
      result = 1;
   }

   return result;
}
