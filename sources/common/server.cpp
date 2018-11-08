// server.cpp

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <asio.hpp>
#include <db.h>

#include <asio.hpp>

#include "database.hpp"
#include "packet.hpp"
#include "syslog.hpp"
#include "server.hpp"

namespace pyzor {

   server::server(syslog& syslog, asio::io_service& io_service, std::string const& address, std::string const& port, pyzor::database& db, bool verbose)
      : syslog_(syslog), io_service_(io_service), address_(address), port_(port), db_(db), verbose_(verbose), socket_(io_service), shutdown_(false)
   {
      db_.start_signal_.connect(boost::bind(&server::start_listening, this));
      db_.stop_signal_.connect(boost::bind(&server::stop_listening, this));
   }

   //

   void server::start_listening()
   {
      syslog_.notice() << "Local database has gone online; starting Pyzor listener on " << address_ << ":" << port_;

      asio::ip::udp::resolver resolver(io_service_);
      asio::ip::udp::resolver::query query(address_, port_);
      asio::ip::udp::resolver::iterator endpoint_iterator = resolver.resolve(query);
      asio::ip::udp::resolver::iterator end;
      
      if (endpoint_iterator == end) {
         throw std::runtime_error(std::string("Cannot resolve hostname for ") + address_);
      }
      
      // Start receiving requests

      asio::ip::udp::endpoint endpoint = *endpoint_iterator;
      
      socket_.open(endpoint.protocol());
      socket_.bind(endpoint);

      socket_.async_receive_from(
         asio::buffer(data_, max_length),
         sender_endpoint_,
         boost::bind(&server::handle_receive_from, this, asio::placeholders::error,  asio::placeholders::bytes_transferred)
      );
   }

   void server::stop_listening()
   {
      syslog_.notice() << "Local database has gone offline; stopping Pyzor listener";
      socket_.close();
   }

   void server::run()
   {
      io_service_.run();
   }

   void server::stop()
   {
      io_service_.stop();
   }

   //

   void server::add_admin_address(std::string const& address)
   {
      admin_addresses_.insert(address.c_str());
   }

   bool server::authorize_admin_request(packet const& request, asio::ip::udp::endpoint const& sender_endpoint_)
   {
      return admin_addresses_.find(sender_endpoint_.address().to_string()) != admin_addresses_.end();
   }

   void server::handle_receive_from(const asio::error_code& error, size_t bytes_recvd)
   {
      if (!error && bytes_recvd > 0)
      {
         // Parse the request
         
         packet res, req;
         
         if (!packet::parse(req, data_, bytes_recvd)) {
            res.set("Thread", req.get("Thread"));
            res.set("PV", "2.0");
            res.set("Code", "400");
            res.set("Diag", "Bad Request");
         } else {
            res.set("Thread", req.get("Thread"));
            res.set("PV", "2.0");
            res.set("Diag", "OK");
            res.set("Code", "200");
            if (req.get("PV") != "2.0") {
               res.set("Code", "505");
               res.set("Diag", "Version Not Supported");                  
            } else {
               request_statistics_.report();
               
               if (req.get("Op") == "shutdown" || req.get("Op") == "statistics") {
                  if (!authorize_admin_request(req, sender_endpoint_)) {
                     res.set("Code", "401");
                     res.set("Diag", "Unauthorized");
                  } else {
                     if (req.get("Op") == "shutdown") {
                        shutdown_ = true;
                     } else if (req.get("Op") == "statistics") {
                        res.set("Stats-Average-Requests", boost::lexical_cast<std::string>(request_statistics_.average()));
                        res.set("Stats-Average-Checks", boost::lexical_cast<std::string>(check_statistics_.average()));
                        res.set("Stats-Average-Hits", boost::lexical_cast<std::string>(hit_statistics_.average()));
                        res.set("Stats-Average-Reports", boost::lexical_cast<std::string>(report_statistics_.average()));
                        res.set("Stats-Average-Whitelists", boost::lexical_cast<std::string>(whitelist_statistics_.average()));
                        res.set("Stats-Total-Requests", boost::lexical_cast<std::string>(request_statistics_.total()));
                        res.set("Stats-Total-Checks", boost::lexical_cast<std::string>(check_statistics_.total()));
                        res.set("Stats-Total-Hits", boost::lexical_cast<std::string>(hit_statistics_.total()));
                        res.set("Stats-Total-Reports", boost::lexical_cast<std::string>(report_statistics_.total()));
                        res.set("Stats-Total-Whitelists", boost::lexical_cast<std::string>(whitelist_statistics_.total()));
                     }
                  }
               } else {
                  if (req.get("Op") == "check") {
                     check_statistics_.report();
                     if (verbose_) {
                        syslog_.debug() << "Request to check digest " << req.get("Op-Digest");
                     }
                     
                     pyzor::record r;

                     res.set("Count", boost::lexical_cast<std::string>(0));
                     res.set("WL-Count", boost::lexical_cast<std::string>(0));

                     if (db_.get(req.get("Op-Digest"), r) == true) {
                        if (r.report_count() == 1 && (time(NULL) - r.entered()) > (3 * 28 * 86400)) {
                           // Ignore records with 1 report that are older than 3 months
                        } else {
                           hit_statistics_.report();
                           res.set("Count", boost::lexical_cast<std::string>(r.report_count()));
                           res.set("WL-Count", boost::lexical_cast<std::string>(r.whitelist_count()));
                        }
                     }
                  } else if (req.get("Op") == "report") {
                     report_statistics_.report();                  
                     if (verbose_) {
                        syslog_.debug() << "Request to report digest " << req.get("Op-Digest");
                     }
                     db_.report(req.get("Op-Digest"));
                  }

                  else if (req.get("Op") == "whitelist") {
                     whitelist_statistics_.report();                     
                     if (verbose_) {
                        syslog_.debug() << "Request to whitelist digest " << req.get("Op-Digest");
                     }                     
#if PYZOR_WHITELIST_ENABLED
                     db_.whitelist(req.get("Op-Digest"));
#endif
                  }

                  else if (req.get("Op") == "ping") {
                     // Nothing to do for ping, just send back a plain response
                  } else {
                     res.set("Code", "501");
                     res.set("Diag", "Not supported operation");
                  }
               }
            }
         }
         
         // Send back the reply
         
         size_t length = res.archive(data_, sizeof(data_));
         
         socket_.async_send_to(
            asio::buffer(data_, length),
            sender_endpoint_,
            boost::bind(&server::handle_send_to, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
         );
      }
      
      // Receive the next incoming packet
      
      if (!shutdown_) {
         socket_.async_receive_from(
            asio::buffer(data_, max_length),
            sender_endpoint_,
            boost::bind(&server::handle_receive_from, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
         );
      }
   }
   
   void server::handle_send_to(const asio::error_code& error, size_t bytes_sent)
   {
      // Nothing
   }
         
}
