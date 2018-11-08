// bohuno-database.cpp

#include "common.hpp"

#include "bohuno-database.hpp"

namespace bohuno {

   static const int IMPORT_BATCH_SIZE = 25000;

   ///

   database::database(boost::filesystem::path const& home)
      : home_(home)
   {
      setup();
   }

   database::~database()
   {
      teardown();
   }

   void database::setup()
   {
      // Create a database environment
            
      int ret = db_env_create(&env_, 0);            
      if (ret != 0) {
         //syslog_.error() << "Cannot create the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      env_->set_encrypt(env_, "BiBiSlRD%^&^PUGtrnRbrrnRbb*&%^@", DB_ENCRYPT_AES);
      
      (void) env_->set_cachesize(env_, 0, 64 * 1024 * 1024, 0);
      (void) env_->set_flags(env_, DB_TXN_NOSYNC, 1);

      env_->set_lk_max_locks(env_, (IMPORT_BATCH_SIZE + (IMPORT_BATCH_SIZE / 10)) * 2);
      env_->set_lk_max_objects(env_, (IMPORT_BATCH_SIZE + (IMPORT_BATCH_SIZE / 10)) * 2);

      // Do deadlock detection internally
      
      ret = env_->set_lk_detect(env_, DB_LOCK_DEFAULT);
      if (ret != 0) {
         //syslog_.error() << "Cannot set automatic lock detection: " << db_strerror(ret);
         throw std::runtime_error(std::string("Cannot set automatic lock detection: ") + db_strerror(ret));
      }

      // Open the environment
      
      u_int32_t flags =  DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_RECOVER;
      
      ret = env_->open(env_, home_.string().c_str(), flags, 0);
      if (ret != 0) {
         //syslog_.error() << "Error while opening the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      // Create a database
      
      ret = db_create(&db_, env_, 0);
      if (ret != 0) {
         //syslog_.error() << "Cannot create the database: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database");
      }
      
      // Open the database
      
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
         //syslog_.error() << "Cannot open the database: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database");
      }

      // Create the index
      
      ret = db_create(&index_, env_, 0);
      if (ret != 0) { 
         //syslog_.error() << "Cannot create the index: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");        
      }

      ret = index_->set_flags(index_, DB_DUP | DB_DUPSORT);
      if (ret != 0) {
         //syslog_.error() << "Cannot set the index flags: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");
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
         //syslog_.error() << "Cannot open the index: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");
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
         //syslog_.error() << "Cannot associate the databasewith the index: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");
      }
   }

   void database::teardown()
   {
      if (index_ != NULL) {
         int ret = index_->close(index_, 0);
         if (ret != 0) {
            //syslog_.error() << "Failed to close the index: " << db_strerror(ret);
         }
      }
      
      if (db_ != NULL) {
         int ret = db_->close(db_, 0);
         if (ret != 0) {
            //syslog_.error() << "Failed to close the database: " << db_strerror(ret);
         }
      }
      
      if (env_ != NULL) {
         int ret = env_->close(env_, 0);
         if (ret != 0) {
            //syslog_.error() << "Error while shutting down the database environment: " << db_strerror(ret);
         }
      }
   }

   bool database::lookup(pyzor::hash const& hash, pyzor::record& record)
   {
      memset(&record, 0, sizeof(pyzor::record));
      
      DBT key;
      memset(&key, 0, sizeof(DBT));
      key.data = (void*) hash.data_;
      key.size = sizeof(hash);
      
      DBT data;
      memset(&data, 0, sizeof(DBT));
      data.data = &record;
      data.ulen = sizeof(pyzor::record);
      data.flags = DB_DBT_USERMEM;
               
      int ret = db_->get(db_, NULL, &key, &data, 0);
      if (ret != 0) {
         if (ret == DB_NOTFOUND) {
            return false;
         } else {
            throw std::runtime_error("Database error");
         }
      } else {
         // We only say the record was found if it was not reset
         return record.report_count() != 0 && record.whitelist_count() != 0;
      }
   }
   
   bool database::lookup_last(pyzor::hash& hash, pyzor::record& record)
   {
      DBC* cursor;
      index_->cursor(index_, NULL, &cursor, 0);
      if (cursor == NULL) {
         throw std::runtime_error("Cannot open cursor");
      }
      
      DBT key, data;
      memset(&key, 0, sizeof(DBT));
      memset(&data, 0, sizeof(DBT));
      
      int ret = cursor->get(cursor, &key, &data, DB_PREV);
      if (ret == 0) {
         pyzor::record* r = static_cast<pyzor::record*>(data.data);
         record = *r;
      } else if (ret == DB_NOTFOUND){
         cursor->close(cursor);
         return false;
      } else {
         cursor->close(cursor);
         throw std::runtime_error("Cannot find last record");
      }
      
      cursor->close(cursor);
      
      return true;
   }
   
   bool database::empty()
   {
      pyzor::hash hash;
      pyzor::record record;
      return !lookup_last(hash, record);
   }
   
   void database::insert(pyzor::hash const& hash, pyzor::record const& record)
   {
      DBT key, data;
      
      memset(&key, 0, sizeof(DBT));
      key.data = (void*) hash.data_;
      key.size = sizeof(pyzor::hash);
      
      memset(&data, 0, sizeof(DBT));
      data.data = (void*) &record;
      data.size = sizeof(pyzor::record);
      
      int ret = db_->put(db_, NULL, &key, &data, 0);
      if (ret != 0) {
         throw std::runtime_error("Cannot insert record");
      }
   }
   
   int database::import(boost::iostreams::filtering_istream& in, database::import_progress_callback callback)
   {
      int n = 0;
      
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
            if (callback) {
               callback(n);
            }
            ret = txn->commit(txn, 0);
            if (ret != 0) {
               throw std::runtime_error("Cannot commit transaction");
            }
            txn = NULL;
         }
         
      }
      
      // Commit the final batch if the transaction is still open
      
      if (txn != NULL) {
         if (callback) {
            callback(n);
         }
         int ret = txn->commit(txn, 0);
         if (ret != 0) {
            throw std::runtime_error("Cannot commit final transaction");
         }
         txn = NULL;
      }
      
      //
            
      return n;
   }

   void database::checkpoint()
   {
      if (env_->txn_checkpoint(env_, 0, 0, 0) == 0) {
         env_->log_archive(env_, NULL, DB_ARCH_REMOVE);
      }
   }
   
}
