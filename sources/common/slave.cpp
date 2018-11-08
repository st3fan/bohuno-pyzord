// slave.cpp

#include <boost/bind.hpp>

#include "common.hpp"
#include "slave.hpp"

#include <errno.h>

#include <stdexcept>

#define MAX_RECORDS_TO_EXPIRE (4 * 3600)

namespace pyzor {

   /// Slave Session

   slave::session::session(asio::io_service& io_service, pyzor::syslog& syslog, pyzor::slave& slave)
      : socket_(io_service), syslog_(syslog), slave_(slave), ping_timer_(io_service), connected_(true)
   {
   }

   slave::session::~session()
   {
      syslog_.notice() << "Destroying session";
   }

   asio::ip::tcp::socket& slave::session::socket()
   {
      return socket_;
   }
   
   void slave::session::start()
   {
      syslog_.debug() << "Starting session";

      asio::async_read(
         socket_,
         asio::buffer(&incoming_update_, sizeof(pyzor::update)),
         boost::bind(&slave::session::handle_read, shared_from_this(), asio::placeholders::error)
      );

      ping_timer_.expires_from_now(boost::posix_time::seconds(3));
      ping_timer_.async_wait(boost::bind(&slave::session::write_ping, shared_from_this()));      
   }
   
   void slave::session::handle_read(const asio::error_code& error)
   {
      if (!error)
      {
         // Ask the slave to deal with this update.
         
         slave_.write_update(incoming_update_);
         
         // Read the next update

         asio::async_read(
            socket_,
            asio::buffer(&incoming_update_, sizeof(pyzor::update)),
            boost::bind(&slave::session::handle_read, shared_from_this(), asio::placeholders::error)
         );
      }
      else
      {
         syslog_.notice() << "Could not read packet from slave session. Closing Socket. Reason: " << error.message();
         socket_.close();
         ping_timer_.cancel();
         connected_ = false;
      }
   }
   
   void slave::session::write_ping()
   {
      static boost::uint32_t ping = 0x42424242;

      if (connected_) {
         asio::async_write(
            socket_,
            asio::buffer(&ping, sizeof(ping)),
            boost::bind(&slave::session::handle_write_ping, shared_from_this(), asio::placeholders::error)
         );
      }
   }

   void slave::session::handle_write_ping(const asio::error_code& error)
   {
      if (!error) {
         ping_timer_.expires_from_now(boost::posix_time::seconds(3));
         ping_timer_.async_wait(boost::bind(&slave::session::write_ping, shared_from_this()));
      } else {
         syslog_.notice() << "Could not ping connected client";
         socket_.close();
         ping_timer_.cancel();
         connected_ = false;
      }
   }

   /// Slave Database

   slave::slave(pyzor::syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home, int cache_size, std::string const& local,
      std::string const& master, std::vector<std::string> const& slaves, bool verbose)
      : syslog_(syslog), io_service_(io_service), home_(home), db_home_(home / "db"), cache_size_(cache_size), local_(local), master_(master), slaves_(slaves),
        verbose_(verbose), env_(NULL), db_(NULL),
        acceptor_(io_service_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5555), true),
        shutdown_(false), socket_(io_service), connect_timer_(io_service_), connected_(false)
   {
      // Setup the database. This will block until syncing is done and replication is going.
      this->setup();
      // Accept connections from clients
      this->accept();
      // Connect to the master
      this->connect();
   }
   
   slave::~slave()
   {
      this->teardown();
   }

   //

   void slave::run()
   {
      io_service_.run();
   }

   void slave::handle_stop()
   {
      // TODO Close all sessions?
      acceptor_.close();
      io_service_.stop();
   }

   void slave::stop()
   {
      io_service_.post(boost::bind(&slave::handle_stop, this));
   }
   
   //

   void slave::log_message(const DB_ENV *dbenv, const char *msg)
   {
      slave* self = (slave*) dbenv->app_private;
      self->syslog_.notice() << "db message: " << std::string(msg);
   }

   void slave::log_error(const DB_ENV *dbenv, const char *errpfx, const char *msg)
   {
      slave* self = (slave*) dbenv->app_private;
      self->syslog_.error() << "db error: " << msg;
   }

   void slave::setup()
   {
      // Create the database directory
      
      if (!boost::filesystem::exists(db_home_)) {
         boost::filesystem::create_directory(db_home_);
      }

      // Create a database environment

      int ret = db_env_create(&env_, 0);            
      if (ret != 0) {
         syslog_.error() << "Cannot create the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database environment");
      }

      env_->app_private = this;
      env_->set_msgcall(env_, &slave::log_message);
      env_->set_errcall(env_, &slave::log_error);

      // Do deadlock detection internally
      
      ret = env_->set_lk_detect(env_, DB_LOCK_DEFAULT);
      if (ret != 0) {
         syslog_.error() << "Cannot set automatic lock detection: " << db_strerror(ret);
         throw std::runtime_error(std::string("Cannot set automatic lock detection") + db_strerror(ret));         
      }

      env_->set_lk_max_locks(env_, (MAX_RECORDS_TO_EXPIRE + (MAX_RECORDS_TO_EXPIRE / 10)) * 10);
      env_->set_lk_max_objects(env_, (MAX_RECORDS_TO_EXPIRE + (MAX_RECORDS_TO_EXPIRE / 10)) * 10);

      (void) env_->set_cachesize(env_, 0, cache_size_ * 1024 * 1024, 0);
      (void) env_->set_flags(env_, DB_TXN_NOSYNC, 1);

      if (verbose_) {
         //(void) env_->set_verbose(env_, DB_VERB_DEADLOCK, 1);
         //(void) env_->set_verbose(env_, DB_VERB_RECOVERY, 1);
         //(void) env_->set_verbose(env_, DB_VERB_REPLICATION, 1);
         //(void) env_->set_verbose(env_, DB_VERB_FILEOPS, 1);
      }
            
      // Configure the replication
            
      ret = env_->rep_set_priority(env_, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot set the replica priority: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      ret = env_->repmgr_set_local_site(env_, local_.c_str(), 5000, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot set the local replica site: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      syslog_.debug() << "Adding master replica " << master_;
      ret = env_->repmgr_add_remote_site(env_, master_.c_str(), 5000, NULL, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot add the master replica site: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      for (std::vector<std::string>::const_iterator i = slaves_.begin(); i != slaves_.end(); ++i) {
         syslog_.debug() << "Adding slave replica " << *i;
         ret = env_->repmgr_add_remote_site(env_, i->c_str(), 5000, NULL, 0);
         if (ret != 0) {
            syslog_.error() << "Cannot add a slave replica site: " << db_strerror(ret);
            throw std::runtime_error("Cannot setup the databse environment");
         }
      }
      
      ret = env_->rep_set_nsites(env_, slaves_.size() + 2); // TODO Waarom 2?
      if (ret != 0) {
         syslog_.error() << "Cannot set the number of replica sites: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      // Open the environment

      ret = env_->open(env_, db_home_.string().c_str(),
         DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_RECOVER | DB_INIT_REP | DB_THREAD, 0);            
      if (ret != 0) {
         syslog_.error() << "Error while opening the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }

      // Start the replication

      ret = env_->repmgr_start(env_, 1, DB_REP_CLIENT);
      if (ret != 0) {
         syslog_.error() << "Error while starting the replication: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup database environment");
      }

      // Setup the database

      while (true)
      {
         // Create a database
            
         int ret = db_create(&db_, env_, 0);
         if (ret != 0) {
            syslog_.error() << "Cannot create the database: " << db_strerror(ret);
            throw std::runtime_error("Cannot setup the database");
         }

         (void) db_->set_pagesize(db_, 4096);
         (void) db_->set_h_hash(db_, &pyzor_hash_function);
            
         // Open the database

         ret = db_->open(db_, NULL, "signatures.db", NULL, DB_HASH, DB_AUTO_COMMIT, 0);            
         if (ret != 0) {
            if (ret == ENOENT || ret == DB_LOCK_DEADLOCK || ret == DB_REP_HANDLE_DEAD) {
               syslog_.notice() << "Cannot open the database: it is not online yet ... retrying in 5 seconds";
               ret = db_->close(db_, 0);
               if (ret != 0) {
                  syslog_.error() << "Cannot close db handle: " << db_strerror(ret);
               }
               db_ = NULL;
               sleep(30);
            } else {
               syslog_.error() << "Cannot open the database, unexpected error: " << db_strerror(ret);
               throw std::runtime_error("Cannot setup the database");
            }
         } else {
            syslog_.notice() << "Database succesfully opened.";
            break;
         }
      }
   }

   void slave::teardown()
   {
#if 0
      if (index_ != NULL) {
         int ret = index_->close(index_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the index: " << db_strerror(ret);
         }
      }
#endif

      if (db_ != NULL) {
         int ret = db_->close(db_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the database: " << db_strerror(ret);
         }
      }

      if (env_ != NULL) {
         int ret = env_->close(env_, 0);
         if (ret != 0) {
            syslog_.error() << "Error while shutting down the database environment: " << db_strerror(ret);
         }
      }
   }

   //

   void slave::accept()
   {
      // Start listening for incoming update sessions
      
      syslog_.notice() << "Accepting connections for updates";

      slave::session_ptr new_session(new slave::session(io_service_, syslog_, *this));

      acceptor_.async_accept(
         new_session->socket(),
         boost::bind(&slave::handle_accept, this, new_session, asio::placeholders::error)
      );
   }

   void slave::handle_accept(slave::session_ptr session, const asio::error_code& error)
   {
      if (!error) {
         session->start();
         slave::session_ptr new_session(new slave::session(io_service_, syslog_, *this));
         acceptor_.async_accept(
            new_session->socket(),
            boost::bind(&slave::handle_accept, this, new_session, asio::placeholders::error)
         );
      }
   }

   //

   void slave::connect()
   {
      socket_.async_connect(
         asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(master_.c_str()), 5555),
         boost::bind(&slave::handle_connect, this, asio::placeholders::error)
      );
   }

   void slave::handle_connect(const asio::error_code& error)
   {
      if (!error)
      {
         connected_ = true;
         syslog_.notice() << "Connected to the master database.";
         
         if (!update_queue_.empty())
         {
            syslog_.debug() << "Sending " << (unsigned int) update_queue_.size() << " queued updates to the master database.";
            
            asio::async_write(
               socket_,
               asio::buffer((void*) &(update_queue_.front()), sizeof(update)),
               boost::bind(&slave::handle_write_update, this, asio::placeholders::error)
            );
         }
      }
      else
      {
         syslog_.debug() << "Could not connect to the master database; retrying after 5 seconds.";
         socket_.close();
         connected_ = false;
         
         connect_timer_.expires_from_now(boost::posix_time::seconds(5));
         connect_timer_.async_wait(boost::bind(&slave::connect, this));      
      }
   }

   void slave::write_update(update u)
   {
      bool write_in_progress = !update_queue_.empty();

      update_queue_.push_back(u);
      
      if (connected_ && !write_in_progress) {
         asio::async_write(
            socket_,
            asio::buffer((void*) &(update_queue_.front()), sizeof(update)),
            boost::bind(&slave::handle_write_update, this, asio::placeholders::error)
         );
      }
   }

   void slave::handle_write_update(const asio::error_code& error)
   {
      if (!error)
      {
         // We're done with the front most item, discard it
         update_queue_.pop_front();
         
         // If there is more to do then we send the next one
         if (connected_ && !update_queue_.empty())
         {
            syslog_.debug() << "Sending " << (unsigned int) update_queue_.size() << " queued updates to the master database.";

            asio::async_write(
               socket_,
               asio::buffer((void*) &(update_queue_.front()), sizeof(update)),
               boost::bind(&slave::handle_write_update, this, asio::placeholders::error)
            );
         }
      }
      else
      {
         syslog_.error() << "Could not send update to the master database; retrying after 5 seconds.";
         socket_.close();
         connected_ = false;
         
         connect_timer_.expires_from_now(boost::posix_time::seconds(5));
         connect_timer_.async_wait(boost::bind(&slave::connect, this));
      }
   }
}
