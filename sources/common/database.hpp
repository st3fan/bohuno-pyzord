// database.hpp

#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/signals.hpp>
#include <asio.hpp>

#include <db.h>

#include "update.hpp"
#include "record.hpp"
#include "syslog.hpp"

namespace pyzor {

   ///

   class database : boost::noncopyable
   {
      public:
         
         database(syslog& syslog, asio::io_service& io_service_, boost::filesystem::path const& home, bool verbose = false);
         ~database();
         
      public:
         
         bool get(std::string const& hexsignature, record& r);
         void get_updated_since(boost::uint32_t since, std::vector<record>& records);
         
         void erase(std::string const& hexsignature);
         void report(std::string const& hexsignature);
         void whitelist(std::string const& hexsignature);
         
         size_t dump_modified_records(boost::filesystem::path const& path, boost::uint32_t min = 0, boost::uint32_t max = 0xffffffff);
         size_t dump_modified_records2(boost::filesystem::path const& path, boost::uint32_t min = 0, boost::uint32_t max = 0xffffffff);

      private:

         static void log_message(const DB_ENV *dbenv, const char *msg);
         static void log_error(const DB_ENV *dbenv, const char *errfx, const char *msg);
         
         void setup();
         void teardown();

      public:

         bool up();

      private:

         void connect();
         void handle_connect(const asio::error_code& error);
         void write_update(update u);
         void handle_write_update(const asio::error_code& error);

         void handle_read_ping(const asio::error_code& error);

      private:
         
         syslog& syslog_;
         asio::io_service& io_service_;
         boost::filesystem::path home_;
         bool verbose_;
         
         DB_ENV* env_;
         DB* db_;
         DB* index_;

         asio::ip::tcp::socket socket_;
         update_queue updates_;
         asio::deadline_timer connect_timer_;
         bool connected_;

      public:

         boost::signal<void ()> start_signal_;
         boost::signal<void ()> stop_signal_;
   };

   typedef boost::shared_ptr<database> database_ptr;

   ///

}

#endif
