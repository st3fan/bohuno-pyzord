

namespace pyzor {

   class client
   {
      public:

         typedef boost::function<void (const asio::error_code& error, packet& res)> callback;

      public:

         client(asio::io_service& io_service, udp::endpoint const& server_endpoint)
            : server_endpoint_(server_endpoint), socket_(io_service, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0))
         {
         }

      public:

         void send_request(packet const& req, udp::endpoint sender_endpoint, callback cb)
         {
            size_t length = req.archive(data_, sizeof(data_));
            socket_.async_send_to(
               asio::buffer(data_, length),
               server_endpoint_,
               boost::bind(&client::handle_send_request, this, asio::placeholders::error, asio::placeholders::bytes_transferred, cb)
            );            
         }
         
         void handle_send_request(const asio::error_code& error, size_t bytes_recvd, callback cb)
         {
            if (!error) {
               receive_from_server(cb);
            } else {
               std::cout << "Receive error" << std::endl;
            }
         }

         void receive_from_server(callback cb)
         {
            socket_.async_receive_from(
               asio::buffer(data_, sizeof(data_)),
               sender_endpoint_,
               boost::bind(&client::handle_receive_from_server, this, asio::placeholders::error,asio::placeholders::bytes_transferred, cb)
            );
         }
         
         void handle_receive_from_server(const asio::error_code& error, size_t bytes_recvd, callback cb)
         {
            if (!error) {
               packet res(data_, bytes_recvd);
               cb(error, res);
            } else {
               std::cout << "Error handle_receive_from_server" << std::endl;
               packet res;
               cb(error, res);
            }
         }

      private:

         udp::endpoint server_endpoint_;
         udp::socket socket_;
         char data_[2048];
         asio::ip::udp::endpoint sender_endpoint_;
   };

   typedef boost::shared_ptr<client> client_ptr;

   class slave_server
   {
      public:
         
         slave_server(syslog_ptr syslog, asio::io_service& io_service, unsigned short port, slave_database_ptr database, client_ptr client, bool verbose = false)
            : syslog_(syslog), port_(port), database_(database), client_(client), verbose_(verbose), socket_(io_service, udp::endpoint(udp::v4(), port)), shutdown_(false)
         {
            // Configure the socket TODO Do we actually need this?
            
            asio::error_code ec;
            asio::socket_base::receive_buffer_size receive_buffer_size(2 * 1024 * 1024);
            socket_.set_option(receive_buffer_size, ec);
            
            // Start receiving requests
            
            socket_.async_receive_from(
               asio::buffer(data_, max_length),
               sender_endpoint_,
               boost::bind(&slave_server::handle_receive_from, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
            );
         }
         
         bool authorize_admin_request(packet const& request, udp::endpoint const& sender_endpoint_)
         {
            return (sender_endpoint_.address() == asio::ip::address::from_string("127.0.0.1"));
         }         
         
         void handle_receive_from(const asio::error_code& error, size_t bytes_recvd)
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
                  res.set("Diag", "OK");
                  res.set("Code", "200");
                  res.set("PV", "2.0");
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
                        if (req.get("Op") == "report" || req.get("Op") == "whitelist") {
                           if (req.get("Op") == "report") {
                              report_statistics_.report();
                           } else if (req.get("Op") == "whitelist") {
                              whitelist_statistics_.report();
                           }
                           if (verbose_) {
                              std::cout << "Request to report or whitelist digest " << req.get("Op-Digest") << std::endl;
                           }                  
                           // Forward this request to the master
                           req.set("X-Original-Sender-Address", sender_endpoint_.address().to_string());
                           req.set("X-Original-Sender-Port", boost::lexical_cast<std::string>(sender_endpoint_.port()));
                           client_->send_request(
                              req,
                              sender_endpoint_,
                              boost::bind(&slave_server::handle_client_request, this, asio::placeholders::error, _2)
                              );
                        } else if (req.get("Op") == "check") {
                           check_statistics_.report();
                           if (verbose_) {
                              std::cout << "Request to check digest " << req.get("Op-Digest") << std::endl;
                           }                  
                           pyzor::record r;
                           (void) database_->lookup(req.get("Op-Digest"), r);
                           res.set("Count", boost::lexical_cast<std::string>(r.report_count()));
                           res.set("WL-Count", boost::lexical_cast<std::string>(r.whitelist_count()));
                        } else if (req.get("Op") == "ping") {
                           // Nothing to do for ping, just send back a plain response
                        } else {
                           res.set("Code", "501");
                           res.set("Diag", "Invalid Operation");
                        }
                     }
                  }
               }

               // Send back a reply               
               size_t length = res.archive(data_, sizeof(data_));                  
               socket_.async_send_to(
                  asio::buffer(data_, length),
                  sender_endpoint_,
                  boost::bind(&slave_server::handle_send_to, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
               );
            }

            // Receive the next incoming packet
            
            if (!shutdown_) {
               socket_.async_receive_from(
                  asio::buffer(data_, max_length),
                  sender_endpoint_,
                  boost::bind(&slave_server::handle_receive_from, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
               );
            }
         }

         void handle_client_request(const asio::error_code& error, packet& res)
         {
            if (!error)
            {
               // Send back a reply to the client

               if (res.has("X-Original-Sender-Address") && res.has("X-Original-Sender-Port"))
               {
                  asio::ip::address address = asio::ip::address::from_string(res.get("X-Original-Sender-Address"));
                  unsigned short port = boost::lexical_cast<unsigned short>(res.get("X-Original-Sender-Port"));
                  asio::ip::udp::endpoint original_sender_endpoint(address, port);
                  
                  res.remove("X-Original-Sender-Address");
                  res.remove("X-Original-Sender-Port");
  
                  size_t length = res.archive(data_, sizeof(data_));
            
                  socket_.async_send_to(
                     asio::buffer(data_, length),
                     original_sender_endpoint,
                     boost::bind(&slave_server::handle_send_to_client, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
                  );
               }
            }
         }

         void handle_send_to_client(const asio::error_code& error, size_t bytes_sent)
         {
         }
         
         void handle_send_to(const asio::error_code& error, size_t bytes_sent)
         {
         }
         
      private:
         
         syslog_ptr syslog_;
         unsigned short port_;
         slave_database_ptr database_;
         client_ptr client_;
         bool verbose_;
         udp::socket socket_;
         udp::endpoint sender_endpoint_;
         enum { max_length = 8192 };
         char data_[max_length];
         bool shutdown_;

         statistics_ring request_statistics_;
         statistics_ring check_statistics_;
         statistics_ring hit_statistics_;
         statistics_ring report_statistics_;
         statistics_ring whitelist_statistics_;
   };

}
