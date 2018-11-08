// slave.hpp

#ifndef PYZOR_SLAVE_HPP
#define PYZOR_SLAVE_HPP

#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <asio.hpp>

#include <db.h>

#include "update.hpp"
#include "record.hpp"
#include "syslog.hpp"

namespace pyzor {

   class slave : boost::noncopyable
   {
      public:

         /// Slave Session

         class session : public boost::enable_shared_from_this<session>
         {
            public:
               
               session(asio::io_service& io_service, pyzor::syslog& syslog, pyzor::slave& slave);
               ~session();
               
            public:
               
               asio::ip::tcp::socket& socket();
               
            public:
               
               void start();
               void handle_read(const asio::error_code& error);

               void write_ping();
               void handle_write_ping(const asio::error_code& error);
               
            private:

               asio::ip::tcp::socket socket_;
               pyzor::syslog& syslog_;
               pyzor::update incoming_update_;
               pyzor::slave& slave_;
               asio::deadline_timer ping_timer_;
               bool connected_;
               
         };
         
         typedef boost::shared_ptr<session> session_ptr;

         friend class session;
         
      public:

         /// Slave Database

         slave(pyzor::syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home, int cache_size,
            std::string const& local, std::string const& master, std::vector<std::string> const& slaves,
            bool verbose = false);
         virtual ~slave();

      public:

         void run();
         void stop();

      private:

         void handle_stop();

      private:

         static void log_message(const DB_ENV *dbenv, const char *msg);
         static void log_error(const DB_ENV *dbenv, const char *errfx, const char *msg);
         
         virtual void setup();
         virtual void teardown();
         
      private:

         void accept();
         void handle_accept(session_ptr session, const asio::error_code& error);

      private:

         void connect();
         void handle_connect(const asio::error_code& error);
         void write_update(update u);
         void handle_write_update(const asio::error_code& error);
         
      private:

         pyzor::syslog& syslog_;
         asio::io_service& io_service_;
         boost::filesystem::path home_;
         boost::filesystem::path db_home_;
         int cache_size_;
         std::string local_;
         std::string master_;
         std::vector<std::string> slaves_;
         bool verbose_;
         
         DB_ENV* env_;
         DB* db_;
         asio::ip::tcp::acceptor acceptor_;
         bool shutdown_;

         asio::ip::tcp::socket socket_;
         update_queue update_queue_;
         asio::deadline_timer connect_timer_;
         bool connected_;
         
   };

   typedef boost::shared_ptr<slave> slave_ptr;

}

#endif // PYZOR_SLAVE_HPP
