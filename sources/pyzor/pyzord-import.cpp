
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <boost/timer.hpp>

#include <db.h>

#define IMPORT_BATCH_SIZE 25000

#include "common.hpp"
#include "dump.hpp"
#include "record.hpp"

struct pyzord_import_options
{
   public:
      
      pyzord_import_options()
         : home("/var/lib/pyzor"), input("pyzor.dump")
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzord-export [-d database-dir] -f input-file" << std::endl;
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
                  input = optarg;
                  break;
               default:
                  usage();
                  return false;
            }
         }

         if (!boost::filesystem::exists(input)) {
            std::cout << "pyzord-import: input file does not exist." << std::endl;
            return false;
         }

         return true;
      }

   public:
      
      boost::filesystem::path home;
      boost::filesystem::path input;
};

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
            boost::filesystem::create_directory(db_home_);
         }

         // Create a database environment
      
         int ret = db_env_create(&env_, 0);            
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot create the databse environment: ") + db_strerror(ret));
         }
         
         // Configure the database environment

         env_->set_lk_max_locks(env_, (IMPORT_BATCH_SIZE + (IMPORT_BATCH_SIZE / 10)) * 2);
         env_->set_lk_max_objects(env_, (IMPORT_BATCH_SIZE + (IMPORT_BATCH_SIZE / 10)) * 2);
         
         (void) env_->set_cachesize(env_, 0, 512 * 1024 * 1024, 0);
         (void) env_->set_flags(env_, DB_TXN_NOSYNC, 1);
      
         // Open the environment
         
         ret = env_->open(
            env_,
            db_home_.string().c_str(),
            DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_RECOVER,
            0
         );   

         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot open the databse environment: ") + db_strerror(ret));
         }
         
         // Create the database
         
         ret = db_create(&db_, env_, 0);
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot create the database: ") + db_strerror(ret));
         }

         (void) db_->set_pagesize(db_, 4096);
   
         ret = db_->open(
            db_,
            NULL,
            "signatures.db",
            NULL,
            DB_HASH,
            DB_CREATE | DB_AUTO_COMMIT,
            0
         );
         
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot open the database: ") + db_strerror(ret));
         }
         
         // Create the index

         ret = db_create(&index_, env_, 0);
         if (ret != 0) { 
            throw std::runtime_error(std::string("Cannot create the index: ") + db_strerror(ret));
         }

         ret = index_->set_flags(index_, DB_DUP | DB_DUPSORT);
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot configure the index: ") + db_strerror(ret));
         }

         ret = index_->open(
            index_,
            NULL,
            "index.db",
            NULL,
            DB_BTREE,
            DB_CREATE | DB_AUTO_COMMIT,
            0
         );

         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot open the index: ") + db_strerror(ret));
         }

         // Associate the index to the primary database
         
         ret = db_->associate(
            db_,
            NULL,
            index_,
            pyzor::create_time_key,
            0
         );
   
         if (ret != 0) {
            throw std::runtime_error(std::string("Cannot associate the index with the database: ") + db_strerror(ret));
         }         
      }
      
      ~database()
      {
         if (index_ != NULL) {
            int ret = index_->close(index_, 0);
            if (ret != 0) {
               // XXX
            }
            index_ = NULL;
         }
         
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

      size_t import(boost::filesystem::path const& path)
      {
         int n = 0;

         // Load the whole dump into memory

         std::vector<char> content;

         std::ifstream file(path.string().c_str(), std::ios::in | std::ios::binary | std::ios::ate);
         if (!file.is_open()) {
            throw std::runtime_error(std::string("cannot open ") + path.string());
         }
         
         size_t size = file.tellg();
         content.resize(size);
         
         file.seekg(0, std::ios::beg);
         file.read((char*) &content[0], size);

         //

         boost::iostreams::filtering_istream in;
         in.push(boost::iostreams::gzip_decompressor());
         in.push(boost::make_iterator_range(content));
      
         //
         
         boost::uint32_t version;
         in.read(reinterpret_cast<char*>(&version), sizeof(version));
         
         //
         
         DB_TXN* txn = NULL;
         
         while (!in.eof())
         {
            // Start a new transaction if we don't have one yet
            
            if (txn == NULL) {
               int ret = env_->txn_begin(env_, NULL, &txn, 0);
               if (ret != 0) {
                  throw std::runtime_error("Cannot create a transaction");
               }
            }
            
            //
            
            pyzor::hash hash;
            in.read(reinterpret_cast<char*>(&hash), sizeof(pyzor::hash));
            if (in.eof()) {
               break;
            }
            
            pyzor::record record;
            in.read(reinterpret_cast<char*>(&record), sizeof(pyzor::record));
            if (in.eof()) {
               break;
            }
            
            //
            
            DBT key, data;
            
            memset(&key, 0, sizeof(DBT));
            key.data = (void*) hash.data_;
            key.size = sizeof(pyzor::hash);
            
            memset(&data, 0, sizeof(DBT));
            data.data = (void*) &record;
            data.size = sizeof(pyzor::record);
            
            int ret = db_->put(db_, txn, &key, &data, 0);
            if (ret != 0) {
               throw std::runtime_error("Cannot insert record");
            }
            
            // If we imported enough then we commit the transaction
            
            n++;
            
            if ((n % IMPORT_BATCH_SIZE) == 0) {
               std::cout << "pyzord-import: imported " << n << " records." << std::endl;
               ret = txn->commit(txn, 0);
               if (ret != 0) {
                  throw std::runtime_error("Cannot commit transaction");
               }
               txn = NULL;
            }
            
         }
         
         // Commit the final batch if the transaction is still open
         
         if (txn != NULL) {
            std::cout << "pyzord-import: imported " << n << " records." << std::endl;
            int ret = txn->commit(txn, 0);
            if (ret != 0) {
               throw std::runtime_error("Cannot commit final transaction");
            }
            txn = NULL;
         }
         
         //
         
         return n;
      }
      
   private:

      boost::filesystem::path home_;
      boost::filesystem::path db_home_;
      
      DB_ENV* env_;
      DB* db_;
      DB* index_;
};

int main(int argc, char** argv)
{
   int result = 0;

   pyzord_import_options options;
   if (options.parse(argc, argv)) {
      try {
         database db(options.home);
         boost::timer timer;
         size_t n = db.import(options.input);
         double elapsed = timer.elapsed();
         std::cout << "pyzord:import: imported " << n << " records in " << elapsed << " seconds (" << (n / elapsed) << " records/second)" << std::endl;
      } catch (std::exception const& e) {
         std::cout << "pyzord-import: could not import records: " << e.what() << std::endl;
         result = 1;
      }
   } else {
      result = 1;
   }

   return result;
}
