// wget.cpp

#ifndef WGET_HPP
#define WGET_HPP

#include <string>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <asio.hpp>
#include <asio/ssl.hpp>

#include "url.hpp"

//

namespace http {

   typedef boost::function<void(const asio::error_code& error)> wget_failure_callback;
   typedef boost::function<void(size_t content_length, size_t read)> wget_progress_callback;
   typedef boost::function<void(unsigned int status, std::map<std::string,std::string> const& headers, std::string& data)> wget_success_callback;

   class client : public boost::enable_shared_from_this<client>
   {
      public:
         
         client(asio::io_service& io_service, std::string const& method, http::url const& url, bool has_post_data, std::string const& post_data,
            wget_failure_callback& failure_cb, wget_success_callback& success_cb, wget_progress_callback& progress_cb);

         void add_request_header(std::string const& name, std::string const& value);
         void set_basic_auth(std::string const& realm, std::string const& username, std::string const& password);
         void start();

      private:
         
         void handle_resolve(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator);
         void handle_connect(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator);
         void handle_write_request(const asio::error_code& error);
         void handle_read_status_line(const asio::error_code& error);
         void handle_read_headers(const asio::error_code& error);
         void handle_read_content(const asio::error_code& error);

      private:

         asio::io_service& io_service_;
         std::string method_;
         http::url url_;
         bool has_post_data_;
         std::string post_data_;
         wget_failure_callback failure_callback_;
         wget_success_callback success_callback_;
         wget_progress_callback progress_callback_;
         asio::ip::tcp::resolver resolver_;
         asio::ip::tcp::socket socket_;
         asio::streambuf request_;
         asio::streambuf response_;
         size_t content_length_;
         std::map<std::string,std::string> headers_;
         std::string data_;
         unsigned int status_code_;
         std::string status_message_;         
         std::map<std::string,std::string> extra_request_headers_;
   };

   typedef boost::shared_ptr<client> client_ptr;

   //

   class sclient : public boost::enable_shared_from_this<sclient>
   {
      public:
         
         sclient(asio::io_service& io_service, std::string const& method, http::url const& url,
            bool has_post_data, std::string const& post_data, wget_failure_callback& failure_cb, wget_success_callback& success_cb,
            wget_progress_callback& progress_cb);

         void add_request_header(std::string const& name, std::string const& value);
         void set_basic_auth(std::string const& realm, std::string const& username, std::string const& password);
         void start();

      private:
         
         void handle_resolve(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator);
         void handle_connect(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator);
         void handle_handshake(const asio::error_code& error);
         void handle_write_request(const asio::error_code& error);
         void handle_read_status_line(const asio::error_code& error);
         void handle_read_headers(const asio::error_code& error);
         void handle_read_content(const asio::error_code& error);

      private:

         asio::io_service& io_service_;
         asio::ssl::context context_;
         std::string method_;
         http::url url_;
         bool has_post_data_;
         std::string post_data_;
         wget_failure_callback failure_callback_;
         wget_success_callback success_callback_;
         wget_progress_callback progress_callback_;
         asio::ip::tcp::resolver resolver_;
         asio::ssl::stream<asio::ip::tcp::socket> socket_;
         asio::streambuf request_;
         asio::streambuf response_;
         size_t content_length_;
         std::map<std::string,std::string> headers_;
         std::string data_;
         unsigned int status_code_;
         std::string status_message_;         
         std::map<std::string,std::string> extra_request_headers_;
   };

   typedef boost::shared_ptr<sclient> sclient_ptr;

   //

   void get(asio::io_service& io_service, http::url const& url, wget_failure_callback failure, wget_success_callback success,
      wget_progress_callback progress = 0L);

   void get(asio::io_service& io_service, http::url const& url, std::string const& username, std::string const& password,
      wget_failure_callback failure, wget_success_callback success, wget_progress_callback progress = 0L);

   void head(asio::io_service& io_service, http::url const& url, wget_failure_callback failure, wget_success_callback success,
      wget_progress_callback progress = 0L);

   void post(asio::io_service& io_service, http::url const& url, std::string const& data, wget_failure_callback failure,
      wget_success_callback success, wget_progress_callback progress = 0L);

   void propget(asio::io_service& io_service, http::url const& url, wget_failure_callback failure,
      wget_success_callback success, wget_progress_callback progress = 0L);

   void propget(asio::io_service& io_service, http::url const& url, std::string const& username, std::string const& password,
      wget_failure_callback failure, wget_success_callback success, wget_progress_callback progress = 0L);
}

#endif
