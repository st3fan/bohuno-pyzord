
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <asio.hpp>

#include "httpd.hpp"

void hello_world_request_handler(const http::server::request& req, http::server::response& response)
{
   response.content_type("text/html");
   response.write("Hello, world!");
}

void random_redirect_request_handler(const http::server::request& req, http::server::response& res)
{
   static char* sites[] = { "http://www.apple.com", "http://www.sun.com", "http://www.microsoft.com" };
   res.redirect(sites[random() % 3]);
}

struct counter_request_handler {
   public:
      counter_request_handler() : count_(0) {}
   public:
      void operator()(const http::server::request& req, http::server::response& response)
      {
         response.content_type("text/html");
         response << "<html><body><p>We've been called " << ++count_ << " times.</p></body></html>";
      }
   private:
      int count_;
};

int main()
{
   asio::io_service io_service;
   
   http::server::server server(io_service, "0.0.0.0", "8088");
   server.register_request_handler("/hello", hello_world_request_handler);
   server.register_request_handler("/redirect", random_redirect_request_handler);
   
   counter_request_handler h;
   server.register_request_handler("/counter", h);

   io_service.run();
   
   return 0;
}
