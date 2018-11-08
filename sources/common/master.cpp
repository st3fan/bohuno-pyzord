// master.cpp

#include "common.hpp"
#include "master.hpp"

#include <stdexcept>
#include <iostream>
#include <fstream>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/timer.hpp>

#define CHECKPOINT_DELAY 300
#define CHECKPOINT_INTERVAL 300

#define EXPIRE_DELAY (15)
#define EXPIRE_INTERVAL (60)
#define MAX_RECORD_AGE (3 * 28 * 86400)
#define MAX_RECORDS_TO_EXPIRE (3600)

namespace pyzor {

   ///

   master::session::session(asio::io_service& io_service, pyzor::syslog& syslog, master& master)
      : socket_(io_service), syslog_(syslog), master_(master), ping_timer_(io_service), connected_(true)
   {
   }

   master::session::~session()
   {
      syslog_.notice() << "Destroying session";
   }

   asio::ip::tcp::socket& master::session::socket()
   {
      return socket_;
   }
   
   void master::session::start()
   {
      syslog_.notice() << "Starting session";

      asio::async_read(
         socket_,
         asio::buffer(&update_, sizeof(update)),
         boost::bind(&master::session::handle_read, shared_from_this(), asio::placeholders::error)
      );

      ping_timer_.expires_from_now(boost::posix_time::seconds(3));
      ping_timer_.async_wait(boost::bind(&master::session::write_ping, shared_from_this()));
   }
   
   void master::session::handle_read(const asio::error_code& error)
   {
      if (!error)
      {
         // Process the update

         master_.process_update(update_);
         
         // Read the next update

         asio::async_read(
            socket_,
            asio::buffer(&update_, sizeof(update)),
            boost::bind(&master::session::handle_read, shared_from_this(), asio::placeholders::error)
         );
      } else {
         syslog_.notice() << "Could not read packet from session. Closing socket. Reason: " << error.message();
         socket_.close();
         ping_timer_.cancel();
         connected_ = false;
      }
   }

   void master::session::write_ping()
   {
      static boost::uint32_t ping = 0x42424242;

      if (connected_) {
         asio::async_write(
            socket_,
            asio::buffer(&ping, sizeof(ping)),
            boost::bind(&master::session::handle_write_ping, shared_from_this(), asio::placeholders::error)
         );
      }
   }

   void master::session::handle_write_ping(const asio::error_code& error)
   {
      if (!error) {
         ping_timer_.expires_from_now(boost::posix_time::seconds(3));
         ping_timer_.async_wait(boost::bind(&master::session::write_ping, shared_from_this()));
      } else {
         syslog_.notice() << "Could not ping connected client";
         socket_.close();
         ping_timer_.cancel();
         connected_ = false;
      }
   }
   
   /// Master Database
   
   master::master(pyzor::syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home, std::string const& local, std::vector<std::string> const& replicas, bool verbose)
      : syslog_(syslog), io_service_(io_service), home_(home), db_home_(home / "db"), local_(local), replicas_(replicas), verbose_(verbose), env_(NULL),
        db_(NULL), index_(NULL),
        global_acceptor_(io_service_, asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(local.c_str()), 5555), true),
        local_acceptor_(io_service_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5555), true),
        checkpoint_timer_(io_service), expire_timer_(io_service)
   {
      // Setup the database

      this->setup_environment();
      this->setup_database();

      // Start listening for incoming update sessions

      master::session_ptr session1(new master::session(io_service_, syslog_, *this));
      local_acceptor_.async_accept(
         session1->socket(),
         boost::bind(&master::handle_local_accept, this, session1, asio::placeholders::error)
      );

      master::session_ptr session2(new master::session(io_service_, syslog_, *this));
      global_acceptor_.async_accept(
         session2->socket(),
         boost::bind(&master::handle_global_accept, this, session2, asio::placeholders::error)
      );

      // Schedule a periodic task to run the checkpoint
      
      this->schedule_checkpoint(CHECKPOINT_DELAY);
      this->schedule_expire(EXPIRE_DELAY);
   }

   master::~master()
   {
      this->shutdown_database();
      this->shutdown_environment();
   }

   //

   void master::run()
   {
      io_service_.run();
   }

   void master::stop()
   {
      io_service_.stop();
   }

   //

   void master::log_message(const DB_ENV *dbenv, const char *msg)
   {
      master* self = (master*) dbenv->app_private;
      self->syslog_.notice() << "db message: " << std::string(msg);
   }

   void master::log_error(const DB_ENV *dbenv, const char *errpfx, const char *msg)
   {
      master* self = (master*) dbenv->app_private;
      self->syslog_.error() << "db error: " << msg;
   }

   void master::setup_environment()
   {
      // Create the database directory
      
      if (!boost::filesystem::exists(db_home_)) {
         boost::filesystem::create_directory(db_home_);
      }

      // Create a database environment
      
      int ret = db_env_create(&env_, 0);            
      if (ret != 0) {
         syslog_.error() << "Cannot create the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      env_->app_private = this;
      env_->set_msgcall(env_, &master::log_message);
      env_->set_errcall(env_, &master::log_error);
      
      (void) env_->set_cachesize(env_, 0, 8 * 1024 * 1024, 0);
      (void) env_->set_flags(env_, DB_TXN_NOSYNC, 1);

      // Do deadlock detection internally
      
      ret = env_->set_lk_detect(env_, DB_LOCK_DEFAULT);
      if (ret != 0) {
         syslog_.error() << "Cannot set automatic lock detection: " << db_strerror(ret);
         throw std::runtime_error(std::string("Cannot set automatic lock detection") + db_strerror(ret));         
      }
      
      env_->set_lk_max_locks(env_, (MAX_RECORDS_TO_EXPIRE + (MAX_RECORDS_TO_EXPIRE / 10)) * 10);
      env_->set_lk_max_objects(env_, (MAX_RECORDS_TO_EXPIRE + (MAX_RECORDS_TO_EXPIRE / 10)) * 10);
      
      if (verbose_) {
         // TODO Set a logging function and log through syslog
         env_->set_verbose(env_, DB_VERB_DEADLOCK, 1);
         env_->set_verbose(env_, DB_VERB_RECOVERY, 1);
      }
      
      // Configure the replication
      
      if (replicas_.size() > 0)
      {
         if (verbose_) {
            // TODO Set a logging function and log through syslog
            env_->set_verbose(env_, DB_VERB_REPLICATION, 1);
         }
         
         env_->rep_set_limit(env_, 0, 32 * 1024);
         env_->repmgr_set_ack_policy(env_, DB_REPMGR_ACKS_NONE);
         
         ret = env_->rep_set_priority(env_, 100);
         if (ret != 0) {
            syslog_.error() << "Cannot set the replica priority: " << db_strerror(ret);
            throw std::runtime_error("Cannot setup the databse environment");
         }
         
         ret = env_->repmgr_set_local_site(env_, local_.c_str(), 5000, 0);
         if (ret != 0) {
            syslog_.error() << "Cannot set the local replica site: " << db_strerror(ret);
            throw std::runtime_error("Cannot setup the databse environment");
         }
         
         for (std::vector<std::string>::const_iterator i = replicas_.begin(); i != replicas_.end(); ++i) {
            syslog_.debug() << "Adding replica " << *i;
            ret = env_->repmgr_add_remote_site(env_, i->c_str(), 5000, NULL, 0);
            if (ret != 0) {
               syslog_.error() << "Cannot add a replica site: " << db_strerror(ret);
               throw std::runtime_error("Cannot setup the databse environment");
            }
         }
         
         ret = env_->rep_set_nsites(env_, replicas_.size() + 1);
         if (ret != 0) {
            syslog_.error() << "Cannot set the number of replica sites: " << db_strerror(ret);
            throw std::runtime_error("Cannot setup the databse environment");
         }
      }
      
      // Open the environment
      
      u_int32_t flags =  DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_RECOVER | DB_THREAD;
      
      if (replicas_.size() > 0) {
         flags |= DB_INIT_REP;
      }

      ret = env_->open(env_, db_home_.string().c_str(), flags, 0);
      if (ret != 0) {
         syslog_.error() << "Error while opening the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }
      
      // Start the replication
      if (replicas_.size() > 0) {
         ret = env_->repmgr_start(env_, 3, DB_REP_MASTER);
         if (ret != 0) {
            syslog_.error() << "Error while starting the replication: " << db_strerror(ret);
                  throw std::runtime_error("Cannot setup database environment");
         }
      }
      
   }
   
   void master::shutdown_environment()
   {
      if (env_ != NULL) {
         int ret = env_->close(env_, 0);
         if (ret != 0) {
            syslog_.error() << "Error while shutting down the database environment: " << db_strerror(ret);
         }
      }
   }
         
   void master::setup_database()
   {
      // Create a database
      
      int ret = db_create(&db_, env_, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot create the database: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database");
      }
      
      //(void) db_->set_h_hash(db_, &pyzor_hash_function);
      
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
         syslog_.error() << "Cannot open the database: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database");
      }

      // Create the index

      ret = db_create(&index_, env_, 0);
      if (ret != 0) { 
         syslog_.error() << "Cannot create the index: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");        
      }

      ret = index_->set_flags(index_, DB_DUP | DB_DUPSORT);
      if (ret != 0) {
         syslog_.error() << "Cannot set the index flags: " << db_strerror(ret);
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
         syslog_.error() << "Cannot open the index: " << db_strerror(ret);
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
         syslog_.error() << "Cannot associate the databasewith the index: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the index");
      }
   }
   
   void master::shutdown_database()
   {
      if (index_ != NULL) {
         int ret = index_->close(index_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the index: " << db_strerror(ret);
         }
      }

      if (db_ != NULL) {
         int ret = db_->close(db_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the database: " << db_strerror(ret);
         }
      }
   }
   
   void master::schedule_checkpoint(int interval)
   {
      checkpoint_timer_.expires_from_now(boost::posix_time::seconds(interval));
      checkpoint_timer_.async_wait(boost::bind(&master::handle_checkpoint, this, asio::placeholders::error));
   }

   void master::handle_checkpoint(const asio::error_code& error)
   {
      if (!error) {
         syslog_.debug() << "Running database checkpoint";
         int ret = env_->txn_checkpoint(env_, 0, 0, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to checkpoint the database: " << db_strerror(ret);            
         } else {
#if 0
            ret = env_->log_archive(env_, NULL, DB_ARCH_REMOVE);
            if (ret != 0) {
               syslog_.error() << "Failed to remove old log files: " << db_strerror(ret);
            }
#endif
         }

         this->schedule_checkpoint(CHECKPOINT_INTERVAL);
      }
   }
   
   boost::uint32_t master::expire(boost::uint32_t updated_from, boost::uint32_t& updated_to)
   {
      boost::uint32_t deleted = 0;

      // Start a transaction
      
      DB_TXN* txn;
         
      int ret = env_->txn_begin(env_, NULL, &txn, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot start a transaction: " << db_strerror(ret);
         throw std::runtime_error("Cannot create a transaction");
      }

      {
         DBT key;
         memset(&key, 0, sizeof(DBT));
      
         // Setup the updated cursor
         
         DBC *updated_cursor;
         ret = index_->cursor(index_, txn, &updated_cursor, 0);
         if (ret != 0) {
            syslog_.error() << "Cannot start create the updated cursor: " << db_strerror(ret);
            throw std::runtime_error("Cannot create the updated cursor");
         }
         
         boost::uint32_t updated = htonl(updated_from);
         DBT updated_key, updated_data;
         memset(&updated_key, 0, sizeof(DBT));
         memset(&updated_data, 0, sizeof(DBT));         
         updated_key.data = &updated;
         updated_key.size = sizeof(boost::uint32_t);
         
         ret = updated_cursor->get(updated_cursor, &updated_key, &updated_data, DB_SET_RANGE);
         if (ret == 0) {
            do
            {
               pyzor::record* r = (pyzor::record*) updated_data.data;
               if (r->updated() >= updated_to) {
                  break;
               }
               
               if (r->report_count() <= 1) {
                  deleted++;
                  updated = r->updated();
                  updated_cursor->del(updated_cursor, 0);  
                  if (deleted == MAX_RECORDS_TO_EXPIRE) {
                     break;
                  }
               }
            }
            while ((ret = updated_cursor->get(updated_cursor, &updated_key, &updated_data, DB_NEXT)) == 0);
            
            updated_to = updated;
         }

         updated_cursor->close(updated_cursor);
      }
      
      // Commit the transaction
      
      ret = txn->commit(txn, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot commit transaction: " << db_strerror(ret);
      }

      return deleted;
   }

   void master::schedule_expire(int interval)
   {
      expire_timer_.expires_from_now(boost::posix_time::seconds(interval));
      expire_timer_.async_wait(boost::bind(&master::handle_expire, this, asio::placeholders::error));
   }
   
   void master::handle_expire(const asio::error_code& error)
   {
      if (!error)
      {
         syslog_.debug() << "\n\nRunning record expiration";

         // Figure out from when we need to check
      
         boost::uint32_t now = time(NULL);
         boost::uint32_t updated_from = 0, updated_to = 0;
      
         boost::filesystem::path expire_status_path = home_ / "expire_status";

         if (boost::filesystem::exists(expire_status_path)) {
            std::ifstream file(expire_status_path.string().c_str(), std::ios::in | std::ios::binary);
            if (!file.is_open()) {
               syslog_.error() << "Cannot open last expiration file even though it exists";
               return;
            }
            file.read((char*) &updated_from, sizeof(boost::uint32_t));
            updated_to = now - MAX_RECORD_AGE;
            file.close();
         } else {
            // Find records that are older than 3 months
            updated_from = 0;
            updated_to = now - MAX_RECORD_AGE;
         }

         std::cout << "Expiring from " << updated_from << " to " << updated_to << std::endl;

         //

         boost::uint32_t deleted = 0;

         try {
            boost::timer timer;
            deleted = expire(updated_from, updated_to);
            syslog_.notice() << "Expired " << deleted << " records in " << timer.elapsed() << " seconds. Last record seen is from " << updated_to;
            if (deleted != 0) {
               // Remember the last record we checked

               std::ofstream file(expire_status_path.string().c_str(), std::ios::out | std::ios::binary);
               if (!file.is_open()) {
                  syslog_.error() << "Cannot open last expiration for writing file even though it exists";
                  return;
               }
               file.write((char*) &updated_to, sizeof(boost::uint32_t));
               file.close();
            }
         } catch (std::exception const& e) {
            syslog_.error() << "Error during record expiration: " << e.what();
         }

         // Reschedule
         
         if (deleted != 0 && (now - MAX_RECORD_AGE - updated_to) > EXPIRE_INTERVAL) {
            syslog_.debug() << "Scheduling next expire soon";
            this->schedule_expire(1);
         } else {
            syslog_.debug() << "Scheduling next expire later";
            this->schedule_expire(EXPIRE_INTERVAL);
         }
      }
   }

   void master::process_update(update const& update)
   {
      switch (update.type())
      {
         case update::report:
            process_report_update(update, true);
            break;
         case update::whitelist:
            process_report_update(update, false);
            break;
         case update::erase:
            process_erase_update(update);
            break;
      }
   }

   void master::process_report_update(update const& update, bool spam)
   {
      pyzor::record r;
      memset(&r, 0, sizeof(record));
      
      // Start a transaction
         
      DB_TXN* txn;
         
      int ret = env_->txn_begin(env_, NULL, &txn, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot start a transaction: " << db_strerror(ret);
         throw std::runtime_error("Cannot create a transaction");
      }
         
      // Try to find the record
         
      DBT key;
      memset(&key, 0, sizeof(DBT));
      key.data = (void*) &(update.ghash());
      key.size = sizeof(hash);
         
      DBT data;
      memset(&data, 0, sizeof(DBT));
      data.data = &r;
      data.ulen = sizeof(record);
      data.flags = DB_DBT_USERMEM;
      
      int result = db_->get(db_, txn, &key, &data, 0);
      if (result == 0 || result == DB_NOTFOUND)
      {
         // Update the record
         
         if (spam) {
            r.report(update.time());
         } else {
            r.whitelist(update.time());
         }
            
         // Write the record back
         
         memset(&key, 0, sizeof(DBT));
         key.data = (void*) &(update.ghash());
         key.size = sizeof(hash);
         
         memset(&data, 0, sizeof(DBT));
         data.data = &r;
         data.size = sizeof(record);
         
         int ret = db_->put(db_, txn, &key, &data, 0);
         if (ret == 0) {
            ret = txn->commit(txn, 0);
            if (ret != 0) {
               syslog_.error() << "Cannot commit transaction: " << db_strerror(ret);
            }
         } else {
            syslog_.error() << "Cannot put record: " << db_strerror(ret);
            txn->abort(txn);
         }
      }
      else
      {
         syslog_.error() << "Cannot get record: " << db_strerror(ret);
         txn->abort(txn);
      }
   }

   /// Erasing a record really means setting it's report and whitelist count to zero
   
   void master::process_erase_update(update const& update)
   {      
      pyzor::record r;
      memset(&r, 0, sizeof(record));
      
      // Start a transaction
         
      DB_TXN* txn;
         
      int ret = env_->txn_begin(env_, NULL, &txn, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot start a transaction: " << db_strerror(ret);
         throw std::runtime_error("Cannot create a transaction");
      }
         
      // Try to find the record
         
      DBT key;
      memset(&key, 0, sizeof(DBT));
      key.data = (void*) &(update.ghash());
      key.size = sizeof(hash);
         
      DBT data;
      memset(&data, 0, sizeof(DBT));
      data.data = &r;
      data.ulen = sizeof(record);
      data.flags = DB_DBT_USERMEM;
      
      int result = db_->get(db_, txn, &key, &data, 0);
      if (result == 0 || result == DB_NOTFOUND)
      {
         // Reset/Delete the record
         
         r.reset();
         
         // Write the record back
         
         memset(&key, 0, sizeof(DBT));
         key.data = (void*) &(update.ghash());
         key.size = sizeof(hash);
         
         memset(&data, 0, sizeof(DBT));
         data.data = &r;
         data.size = sizeof(record);
         
         int ret = db_->put(db_, txn, &key, &data, 0);
         if (ret == 0) {
            ret = txn->commit(txn, 0);
            if (ret != 0) {
               syslog_.error() << "Cannot commit transaction: " << db_strerror(ret);
            }
         } else {
            syslog_.error() << "Cannot put record: " << db_strerror(ret);
            txn->abort(txn);
         }
      }
      else
      {
         syslog_.error() << "Cannot get record: " << db_strerror(ret);
         txn->abort(txn);
      }
   }

   void master::handle_local_accept(master::session_ptr session, const asio::error_code& error)
   {
      if (!error) {
         session->start();
         master::session_ptr new_session(new master::session(io_service_, syslog_, *this));
         local_acceptor_.async_accept(
            new_session->socket(),
            boost::bind(&master::handle_local_accept, this, new_session, asio::placeholders::error)
         );
      }
   }

   void master::handle_global_accept(master::session_ptr session, const asio::error_code& error)
   {
      if (!error) {
         session->start();
         master::session_ptr new_session(new master::session(io_service_, syslog_, *this));
         global_acceptor_.async_accept(
            new_session->socket(),
            boost::bind(&master::handle_global_accept, this, new_session, asio::placeholders::error)
         );
      }
   }

}
