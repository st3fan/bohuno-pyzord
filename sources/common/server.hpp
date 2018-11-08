// server.hpp

#ifndef PYZOR_SERVER_HPP
#define PYZOR_SERVER_HPP

#include <time.h>
#include <string.h>

#include <set>
#include <string>

#include <asio.hpp>

#include "database.hpp"
#include "hash.hpp"
#include "record.hpp"
#include "statistics.hpp"
#include "syslog.hpp"

namespace pyzor {

   class server
   {
      public:
         
         server(syslog& syslog, asio::io_service& io_service, std::string const& address, std::string const& port, pyzor::database& db, bool verbose = false);

      public:

         void start_listening();
         void stop_listening();
         
         void run();
         void stop();

         void add_admin_address(std::string const& address);         
         bool authorize_admin_request(packet const& request, asio::ip::udp::endpoint const& sender_endpoint_);
         void handle_receive_from(const asio::error_code& error, size_t bytes_recvd);
         void handle_send_to(const asio::error_code& error, size_t bytes_sent);
            
      private:

         syslog& syslog_;
         asio::io_service& io_service_;
         std::string address_;
         std::string port_;
         database& db_;
         bool verbose_;
         asio::ip::udp::socket socket_;
         asio::ip::udp::endpoint sender_endpoint_;
         enum { max_length = 8192 };
         char data_[max_length];
         bool shutdown_;

         statistics_ring request_statistics_;
         statistics_ring check_statistics_;
         statistics_ring hit_statistics_;
         statistics_ring report_statistics_;
         statistics_ring whitelist_statistics_;

         std::set<std::string> admin_addresses_;
   };

}

#endif
