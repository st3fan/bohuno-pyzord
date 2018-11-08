
#ifndef PYZOR_MASTER_HPP
#define PYZOR_MASTER_HPP

#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <asio.hpp>

#include <db.h>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <asio.hpp>

#include "record.hpp"
#include "update.hpp"
#include "syslog.hpp"

namespace pyzor {

   ///

   class master
   {
      public:

         ///

         class session : public boost::enable_shared_from_this<session>
         {
            public:
               
               session(asio::io_service& io_service, pyzor::syslog& syslog, master& master);
               ~session();

            public:
               
               asio::ip::tcp::socket& socket();
               
            public:
               
               void start();
               void handle_read(const asio::error_code& error);
               
            public:
               
               void write_ping();
               void handle_write_ping(const asio::error_code& error);
               
            private:
               
               asio::ip::tcp::socket socket_;
               pyzor::syslog& syslog_;
               update update_;
               master& master_;
               asio::deadline_timer ping_timer_;
               bool connected_;
         };
         
         typedef boost::shared_ptr<session> session_ptr;
         
      public:

         master(pyzor::syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home, std::string const& local, std::vector<std::string> const& replicas, bool verbose = false);
         virtual ~master();

      public:
         
         void run();
         void stop();

      private:

         static void log_message(const DB_ENV *dbenv, const char *msg);
         static void log_error(const DB_ENV *dbenv, const char *errfx, const char *msg);

         virtual void setup_environment();
         virtual void shutdown_environment();
         virtual void setup_database();
         virtual void shutdown_database();

      private:

         void schedule_checkpoint(int interval);
         void handle_checkpoint(const asio::error_code& error);

         boost::uint32_t expire(boost::uint32_t updated_from, boost::uint32_t& updated_to);
         void schedule_expire(int interval);
         void handle_expire(const asio::error_code& error);

      public:
         
         void process_update(update const& update);
         void process_report_update(update const& update, bool spam);
         void process_erase_update(update const& update);

      private:

         void handle_local_accept(session_ptr session, const asio::error_code& error);
         void handle_global_accept(session_ptr session, const asio::error_code& error);
            
      private:

         pyzor::syslog& syslog_;
         asio::io_service& io_service_;
         boost::filesystem::path home_;
         boost::filesystem::path db_home_;
         std::string local_;
         std::vector<std::string> replicas_;
         bool verbose_;
         DB_ENV* env_;
         DB* db_;
         DB* index_;
         asio::ip::tcp::acceptor global_acceptor_;
         asio::ip::tcp::acceptor local_acceptor_;
         asio::deadline_timer checkpoint_timer_;
         asio::deadline_timer expire_timer_;
   };

   typedef boost::shared_ptr<master> master_ptr;

}

#endif // PYZOR_MASTER_HPP
