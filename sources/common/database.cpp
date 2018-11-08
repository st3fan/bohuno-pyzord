// database.cpp

#include <sys/errno.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/bind.hpp>
#include <asio.hpp>

#include "common.hpp"
#include "database.hpp"

namespace pyzor {

   // Client Database

   database::database(syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home, bool verbose)
      : syslog_(syslog), io_service_(io_service), home_(home), verbose_(verbose),
        env_(NULL), db_(NULL), index_(NULL), socket_(io_service), connect_timer_(io_service_), connected_(false)
   {
      this->connect();
   }
   
   database::~database()
   {
      this->teardown();
   }

   //

   bool database::get(std::string const& hexsignature, record& r)
   {
      memset(&r, 0, sizeof(record));

      unsigned char signature[20];
      if (pyzor::decode_signature(hexsignature, signature))
      {
         DBT key;
         memset(&key, 0, sizeof(DBT));
         key.data = signature;
         key.size = sizeof(signature);
         
         DBT data;
         memset(&data, 0, sizeof(DBT));
         data.data = &r;
         data.ulen = sizeof(record);
         data.flags = DB_DBT_USERMEM;
         
         int ret = db_->get(db_, NULL, &key, &data, 0);
         if (ret != 0) {
            if (ret == DB_NOTFOUND) {
               return false;
            } else {
               throw std::runtime_error(std::string("Database failure"));
            }
         } else {
            // We only say the record was found if it was not reset
	    if (r.report_count() == 0 && r.whitelist_count() == 0) {
              return false;
	    } else {
              return true;
	    }
         }
      }
      
      return false;
   }

   void database::get_updated_since(boost::uint32_t since, std::vector<record>& records)
   {
      std::cout << "GET-UPDATED-SINCE: Getting records updated since " << since << std::endl;

      DBC *cursor;

      index_->cursor(index_, NULL, &cursor, 0);

      record r;
      boost::uint32_t t = htonl(since);

      DBT key;
      memset(&key, 0, sizeof(DBT));
      key.data = &t;
      key.size = sizeof(t);
      
      DBT data;
      memset(&data, 0, sizeof(DBT));
      data.data = &r;
      data.ulen = sizeof(record);
      data.flags = DB_DBT_USERMEM;

      int n = 0;
      
      int ret = cursor->get(cursor, &key, &data, DB_SET_RANGE);      
      if (ret == 0) {
         do {
            n++;
            records.push_back(r);
         } while ((ret = cursor->get(cursor, &key, &data, DB_NEXT)) != DB_NOTFOUND);
      }
   }
   
   void database::erase(std::string const& hash)
   {
      update u(hash, update::erase);
      io_service_.post(boost::bind(&database::write_update, this, u));
   }

   void database::report(std::string const& hash)
   {
      update u(hash, update::report);
      io_service_.post(boost::bind(&database::write_update, this, u));
   }

   void database::whitelist(std::string const& hash)
   {
      update u(hash, update::whitelist);
      io_service_.post(boost::bind(&database::write_update, this, u));
   }

   size_t database::dump_modified_records(boost::filesystem::path const& path, boost::uint32_t min, boost::uint32_t max)
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
         record* r = static_cast<record*>(data.data);
         if (r->updated() >= min && r->updated() <= max) {
            out.write(reinterpret_cast<char*>(key.data), key.size);
            out.write(reinterpret_cast<char*>(data.data), data.size);
            n++;
         }
      }

      cursor->close(cursor);      

      return n;
   }

   size_t database::dump_modified_records2(boost::filesystem::path const& path, boost::uint32_t min, boost::uint32_t max)
   {
      boost::iostreams::filtering_ostream out;
      out.push(boost::iostreams::gzip_compressor());
      out.push(boost::iostreams::file_sink(path.string(), std::ios::binary));

      // Print the header

      boost::uint32_t header = htonl(2);
      out.write(reinterpret_cast<char*>(&header), sizeof(header));      
      
      // Find all matching records

      DBC *cursor;

      DBT key, pkey, pdata;

      memset(&key, 0, sizeof(DBT));
      memset(&pkey, 0, sizeof(DBT));
      memset(&pdata, 0, sizeof(DBT));

      boost::uint32_t timestamp = htonl(min);
      key.data = &timestamp;
      key.size = sizeof(boost::uint32_t);

      index_->cursor(index_, NULL, &cursor, 0);

      size_t n = 0;

      boost::uint32_t xmin = 0xffffffff, xmax = 0;

      int ret = cursor->pget(cursor, &key, &pkey, &pdata, DB_SET_RANGE);
      if (ret == 0) {
         while ((ret = cursor->pget(cursor, &key, &pkey, &pdata, DB_NEXT)) == 0) {
            record* r = static_cast<record*>(pdata.data);
            if (r->updated() > max) {
               break;
            }
            out.write(reinterpret_cast<char*>(pkey.data), pkey.size);
            out.write(reinterpret_cast<char*>(pdata.data), pdata.size);

            if (r->updated() < xmin) {
               xmin = r->updated();
            }

            if (r->updated() > xmax) {
               xmax = r->updated();
            }

            n++;
         }
      }

      cursor->close(cursor);

      return n;
   }

   //

   void database::log_message(const DB_ENV *dbenv, const char *msg)
   {
      database* self = (database*) dbenv->app_private;
      self->syslog_.notice() << "db message:" << std::string(msg);
   }

   void database::log_error(const DB_ENV *dbenv, const char *errpfx, const char *msg)
   {
      database* self = (database*) dbenv->app_private;
      self->syslog_.error() << "db error: " << msg;
   }

   void database::setup()
   {
      // Create the database directory

      boost::filesystem::path db_home = home_ / "db";
      if (!boost::filesystem::exists(db_home)) {
         boost::filesystem::create_directory(db_home);
      }

      // Create a database environment
      
      int ret = db_env_create(&env_, 0);            
      if (ret != 0) {
         syslog_.error() << "Cannot create the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }
      
      env_->app_private = this;
      env_->set_msgcall(env_, &database::log_message);
      env_->set_errcall(env_, &database::log_error);
      
      // Open the environment
      
      u_int32_t flags =  DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_THREAD;
      
      ret = env_->open(env_, db_home.string().c_str(), flags, 0);
      if (ret != 0) {
         syslog_.error() << "Error while opening the database environment: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the databse environment");
      }
      
      // Setup the databases

      // Create the database

      ret = db_create(&db_, env_, 0);
      if (ret != 0) {
         syslog_.error() << "Cannot create the database: " << db_strerror(ret);
         throw std::runtime_error("Cannot setup the database");
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
         DB_UNKNOWN,
         DB_RDONLY | DB_THREAD,
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

   void database::teardown()
   {
      if (index_ != NULL) {
         int ret = index_->close(index_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the index: " << db_strerror(ret);
         }
         index_ = NULL;
      }

      if (db_ != NULL) {
         int ret = db_->close(db_, 0);
         if (ret != 0) {
            syslog_.error() << "Failed to close the database: " << db_strerror(ret);
         }
         db_ = NULL;
      }
      
      if (env_ != NULL) {
         int ret = env_->close(env_, 0);
         if (ret != 0) {
            syslog_.error() << "Error while shutting down the database environment: " << db_strerror(ret);
         }
         env_ = NULL;
      }
   }

   //

   bool database::up()
   {
      return connected_;
   }

   //

   void database::connect()
   {
      socket_.async_connect(
         asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5555),
         boost::bind(&database::handle_connect, this, asio::placeholders::error)
      );
   }

   void database::handle_connect(const asio::error_code& error)
   {
      static boost::uint32_t ping;

      if (!error) {
         connected_ = true;
         syslog_.notice() << "Connected to the local database.";
         
         this->setup();
         start_signal_();
         
         if (!updates_.empty()) {
            syslog_.notice() << "There are " << (unsigned int) updates_.size() << " updates queued. Sending them.";
            asio::async_write(
               socket_,
               asio::buffer((void*) &(updates_.front()), sizeof(update)),
               boost::bind(&database::handle_write_update, this, asio::placeholders::error)
            );
         }
         // Read a ping
         asio::async_read(
            socket_,
            asio::buffer(&ping, sizeof(ping)),
            boost::bind(&database::handle_read_ping, this, asio::placeholders::error)
         );
      } else {
         syslog_.notice() << "Could not connect to the local database; retrying after 5 seconds.";
         socket_.close();
         connect_timer_.expires_from_now(boost::posix_time::seconds(5));
         connect_timer_.async_wait(boost::bind(&database::connect, this));      
      }
   }

   void database::handle_read_ping(const asio::error_code& error)
   {
      static boost::uint32_t ping;
      
      if (!error) {
         asio::async_read(
            socket_,
            asio::buffer(&ping, sizeof(ping)),
            boost::bind(&database::handle_read_ping, this, asio::placeholders::error)
         );         
      } else {
         syslog_.notice() << "Failed to read a ping from the server. Reconnecting after 5 seconds.";
         socket_.close();

         stop_signal_();
         this->teardown();

         connect_timer_.expires_from_now(boost::posix_time::seconds(5));
         connect_timer_.async_wait(boost::bind(&database::connect, this));               
      }
   }
      
   void database::write_update(update u)
   {
      bool write_in_progress = !updates_.empty();      
      updates_.push_back(u);      
      if (connected_ && !write_in_progress) {
         asio::async_write(
            socket_,
            asio::buffer((void*) &(updates_.front()), sizeof(update)),
            boost::bind(&database::handle_write_update, this, asio::placeholders::error)
         );
      }
   }
      
   void database::handle_write_update(const asio::error_code& error)
   {
      if (!error) {
         updates_.pop_front();
         if (connected_ && !updates_.empty()) {
            asio::async_write(
               socket_,
               asio::buffer((void*) &(updates_.front()), sizeof(update)),
               boost::bind(&database::handle_write_update, this, asio::placeholders::error)
            );
         }
      } else {
         syslog_.notice() << "Could not send update to the master server; reconnecting after 5 seconds.";
         socket_.close();
         connected_ = false;

         stop_signal_();
         this->teardown();

         connect_timer_.expires_from_now(boost::posix_time::seconds(5));
         connect_timer_.async_wait(boost::bind(&database::connect, this));
      }
   }
   
}
