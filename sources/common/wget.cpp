// wget.cpp

#include <iostream>
#include <string>
#include <stdexcept>

#include <boost/algorithm/string/trim.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <asio.hpp>

#include "base64.hpp"
#include "url.hpp"
#include "wget.hpp"

//

namespace http {

   // TODO Merge these into one; this is silly

   client::client(asio::io_service& io_service, std::string const& method, http::url const& url, bool has_post_data,
      std::string const& post_data,
      wget_failure_callback& failure_cb, wget_success_callback& success_cb, wget_progress_callback& progress_cb)
      : io_service_(io_service), method_(method), url_(url), has_post_data_(has_post_data), post_data_(post_data),
        failure_callback_(failure_cb), success_callback_(success_cb), progress_callback_(progress_cb),
        resolver_(io_service), socket_(io_service), content_length_(0)
   {
      if (url.scheme() != "http") {
         throw std::runtime_error("Invalid scheme, only http is supported");
      }
   }

   void client::add_request_header(std::string const& name, std::string const& value)
   {
      extra_request_headers_[name] = value;
   }

   void client::set_basic_auth(std::string const& realm, std::string const& username, std::string const& password)
   {
      extra_request_headers_["Authorization"] = std::string("Basic ") + base64::encode(username + ":" + password);
   }
   
   void client::start()
   {
      std::ostream request_stream(&request_);
      request_stream << method_ << " " << url_.path() << " HTTP/1.0\r\n";
      
      for (std::map<std::string,std::string>::const_iterator i = extra_request_headers_.begin(); i != extra_request_headers_.end(); ++i) {
         request_stream << i->first << ": " << i->second << "\r\n";
      }
      
      request_stream << "Host: " << url_.host() << "\r\n";
      request_stream << "Accept: */*\r\n";
      request_stream << "Connection: close\r\n\r\n";
      
      asio::ip::tcp::resolver::query query(url_.host(), boost::lexical_cast<std::string>(url_.port()));
      resolver_.async_resolve(
         query,
         boost::bind(&client::handle_resolve, shared_from_this(), asio::placeholders::error, asio::placeholders::iterator)
      );
   }
   
   void client::handle_resolve(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator)
   {
      if (!error) {
         // Attempt a connection to the first endpoint in the list. Each endpoint
         // will be tried until we successfully establish a connection.
         asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
         socket_.async_connect(
            endpoint,
            boost::bind(&client::handle_connect, shared_from_this(), asio::placeholders::error, ++endpoint_iterator)
            );
      } else {
         failure_callback_(error);
      }
   }
   
   void client::handle_connect(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator)
   {
      if (!error) {
         // The connection was successful. Send the request.
         asio::async_write(
            socket_,
            request_,
            boost::bind(&client::handle_write_request, shared_from_this(), asio::placeholders::error)
            );
      } else if (endpoint_iterator != asio::ip::tcp::resolver::iterator()) {
         // The connection failed. Try the next endpoint in the list.
         socket_.close();
         asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
         socket_.async_connect(
            endpoint,
            boost::bind(&client::handle_connect, shared_from_this(), asio::placeholders::error, ++endpoint_iterator)
            );
      } else {
         failure_callback_(error);
      }
   }
   
   void client::handle_write_request(const asio::error_code& error)
   {
      if (!error) {
         // Read the response status line.
         asio::async_read_until(
            socket_,
            response_,
            "\r\n",
            boost::bind(&client::handle_read_status_line, shared_from_this(), asio::placeholders::error)
            );
      } else {
         failure_callback_(error);
      }
   }
   
   void client::handle_read_status_line(const asio::error_code& error)
   {
      if (error) {
         failure_callback_(error);
         return;
      }
      
      // Check that response is OK.
      std::istream response_stream(&response_);
      std::string http_version;
      response_stream >> http_version;
      response_stream >> status_code_;
      std::getline(response_stream, status_message_);
      
      if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
         failure_callback_(asio::error_code());
         return;
      }
      
      // Read the response headers, which are terminated by a blank line.
      asio::async_read_until(
         socket_,
         response_,
         "\r\n\r\n",
         boost::bind(&client::handle_read_headers, shared_from_this(), asio::placeholders::error)
         );
   }
   
   void client::handle_read_headers(const asio::error_code& error)
   {
      if (!error)
      {
         // Process the response headers.
         std::istream response_stream(&response_);
         std::string header;
         
         boost::regex regex("^(\\S+?):\\s+(.*)$", boost::regex::perl);
         
         while (std::getline(response_stream, header) && header != "\r") {
            // Get rid of whitespace at the end
            boost::algorithm::trim(header);
            // Parse the header
            boost::smatch matches;
            if (boost::regex_match(header, matches, regex)) {
               // Remember all headers
               headers_[matches[1].str()] = matches[2].str();
               // Take the content length for the progress indicator
               if (matches[1].str() == "Content-Length") {
                  content_length_ = boost::lexical_cast<size_t>(matches[2].str());
               }
            }
            //std::cout << header << "\n";
         }
         //std::cout << "\n";
         
         // Write whatever content we already have to output.
         if (response_.size() > 0)
         {
            char ch;
            do {
               ch = response_.sgetc();
               data_.push_back(ch);
            } while (response_.snextc()!=EOF);
         }
         
         // Fire the progress callback
         if (progress_callback_) {
            progress_callback_(content_length_, 0);
         }
         
         // Start reading remaining data until EOF.
         asio::async_read(
            socket_,
            response_,
            asio::transfer_at_least(1),
            boost::bind(&client::handle_read_content, shared_from_this(), asio::placeholders::error)
            );
      }
      else
      {
         failure_callback_(error);
      }
   }
   
   void client::handle_read_content(const asio::error_code& error)
   {
      if (!error)
      {
         // Write all of the data that has been read so far.
         char ch;
         do {
            ch = response_.sgetc();
            data_.push_back(ch);
         } while (response_.snextc()!=EOF);
               
         // Fire the progress callback
         if (progress_callback_) {
            progress_callback_(content_length_, data_.length());
         }
               
         // Continue reading remaining data until EOF.
         asio::async_read(
            socket_,
            response_,
            asio::transfer_at_least(1),
            boost::bind(&client::handle_read_content, shared_from_this(), asio::placeholders::error)
            );
      }
      else if (error == asio::error::eof)
      {
         success_callback_(status_code_, headers_, data_);
      }
      else
      {
         failure_callback_(error);
      }
   }

   //

   sclient::sclient(asio::io_service& io_service, std::string const& method, http::url const& url,
      bool has_post_data, std::string const& post_data,
      wget_failure_callback& failure_cb, wget_success_callback& success_cb, wget_progress_callback& progress_cb)
      : io_service_(io_service), context_(io_service_, asio::ssl::context::sslv23), method_(method), url_(url), has_post_data_(has_post_data), post_data_(post_data),
        failure_callback_(failure_cb), success_callback_(success_cb), progress_callback_(progress_cb),
        resolver_(io_service), socket_(io_service_, context_), content_length_(0)
   {
      if (url.scheme() != "https") {
         throw std::runtime_error("Invalid scheme, only https is supported");
      }
   }

   void sclient::add_request_header(std::string const& name, std::string const& value)
   {
      extra_request_headers_[name] = value;
   }

   void sclient::set_basic_auth(std::string const& realm, std::string const& username, std::string const& password)
   {
      extra_request_headers_["Authorization"] = std::string("Basic ") + base64::encode(username + ":" + password);
   }
   
   void sclient::start()
   {
      std::ostream request_stream(&request_);
      request_stream << method_ << " " << url_.path() << " HTTP/1.0\r\n";
      
      for (std::map<std::string,std::string>::const_iterator i = extra_request_headers_.begin(); i != extra_request_headers_.end(); ++i) {
         request_stream << i->first << ": " << i->second << "\r\n";
      }
      
      request_stream << "Host: " << url_.host() << "\r\n";
      request_stream << "Accept: */*\r\n";
      request_stream << "Connection: close\r\n\r\n";
      
      asio::ip::tcp::resolver::query query(url_.host(), boost::lexical_cast<std::string>(url_.port()));
      resolver_.async_resolve(
         query,
         boost::bind(&sclient::handle_resolve, shared_from_this(), asio::placeholders::error, asio::placeholders::iterator)
      );
   }
   
   void sclient::handle_resolve(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator)
   {
      if (!error) {
         // Attempt a connection to the first endpoint in the list. Each endpoint
         // will be tried until we successfully establish a connection.
         asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
         socket_.lowest_layer().async_connect(
            endpoint,
            boost::bind(&sclient::handle_connect, shared_from_this(), asio::placeholders::error, ++endpoint_iterator)
         );
      } else {
         failure_callback_(error);
      }
   }
   
   void sclient::handle_connect(const asio::error_code& error, asio::ip::tcp::resolver::iterator endpoint_iterator)
   {
      if (!error) {
         // The connection was successful. Do the SSL handshake
         socket_.async_handshake(
            asio::ssl::stream_base::client,
            boost::bind(&sclient::handle_handshake, shared_from_this(), asio::placeholders::error)
         );
      } else if (endpoint_iterator != asio::ip::tcp::resolver::iterator()) {
         // The connection failed. Try the next endpoint in the list.
         socket_.lowest_layer().close();
         asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
         socket_.lowest_layer().async_connect(
            endpoint,
            boost::bind(&sclient::handle_connect, shared_from_this(), asio::placeholders::error, ++endpoint_iterator)
            );
      } else {
         failure_callback_(error);
      }
   }

   void sclient::handle_handshake(const asio::error_code& error)
   {
      if (!error) {
         // The handshake was successful. Send the request.
         asio::async_write(
            socket_,
            request_,
            boost::bind(&sclient::handle_write_request, shared_from_this(), asio::placeholders::error)
         );         
      } else {
         failure_callback_(error);
      }
   }
   
   void sclient::handle_write_request(const asio::error_code& error)
   {
      if (!error) {
         // Read the response status line.
         asio::async_read_until(
            socket_,
            response_,
            "\r\n",
            boost::bind(&sclient::handle_read_status_line, shared_from_this(), asio::placeholders::error)
            );
      } else {
         failure_callback_(error);
      }
   }
   
   void sclient::handle_read_status_line(const asio::error_code& error)
   {
      if (error) {
         failure_callback_(error);
         return;
      }
      
      // Check that response is OK.
      std::istream response_stream(&response_);
      std::string http_version;
      response_stream >> http_version;
      response_stream >> status_code_;
      std::getline(response_stream, status_message_);
      
      if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
         failure_callback_(asio::error_code());
         return;
      }
      
      // Read the response headers, which are terminated by a blank line.
      asio::async_read_until(
         socket_,
         response_,
         "\r\n\r\n",
         boost::bind(&sclient::handle_read_headers, shared_from_this(), asio::placeholders::error)
         );
   }
   
   void sclient::handle_read_headers(const asio::error_code& error)
   {
      if (!error)
      {
         // Process the response headers.
         std::istream response_stream(&response_);
         std::string header;
         
         boost::regex regex("^(\\S+?):\\s+(.*)$", boost::regex::perl);
         
         while (std::getline(response_stream, header) && header != "\r") {
            // Get rid of whitespace at the end
            boost::algorithm::trim(header);
            // Parse the header
            boost::smatch matches;
            if (boost::regex_match(header, matches, regex)) {
               // Remember all headers
               headers_[matches[1].str()] = matches[2].str();
               // Take the content length for the progress indicator
               if (matches[1].str() == "Content-Length") {
                  content_length_ = boost::lexical_cast<size_t>(matches[2].str());
               }
            }
            //std::cout << header << "\n";
         }
         //std::cout << "\n";
         
         // Write whatever content we already have to output.
         if (response_.size() > 0)
         {
            char ch;
            do {
               ch = response_.sgetc();
               data_.push_back(ch);
            } while (response_.snextc()!=EOF);
         }
         
         // Fire the progress callback
         if (progress_callback_) {
            progress_callback_(content_length_, 0);
         }
         
         // Start reading remaining data until EOF.
         asio::async_read(
            socket_,
            response_,
            asio::transfer_at_least(1),
            boost::bind(&sclient::handle_read_content, shared_from_this(), asio::placeholders::error)
            );
      }
      else
      {
         failure_callback_(error);
      }
   }
   
   void sclient::handle_read_content(const asio::error_code& error)
   {
      if (!error)
      {
         // Write all of the data that has been read so far.
         char ch;
         do {
            ch = response_.sgetc();
            data_.push_back(ch);
         } while (response_.snextc()!=EOF);
               
         // Fire the progress callback
         if (progress_callback_) {
            progress_callback_(content_length_, data_.length());
         }
               
         // Continue reading remaining data until EOF.
         asio::async_read(
            socket_,
            response_,
            asio::transfer_at_least(1),
            boost::bind(&sclient::handle_read_content, shared_from_this(), asio::placeholders::error)
         );
      }
      else if (error == asio::error::eof)
      {
         success_callback_(status_code_, headers_, data_);
      }
      else
      {
         failure_callback_(error);
      }
   }

   //

   void get(asio::io_service& io_service, http::url const& url, wget_failure_callback failure, wget_success_callback success,
      wget_progress_callback progress)
   {
      if (url.scheme() == "http") {
         client_ptr c(new client(io_service, "GET", url, false, "", failure, success, progress));
         c->start();
      } else {
         sclient_ptr c(new sclient(io_service, "GET", url, false, "", failure, success, progress));
         c->start();
      }
   }

   void get(asio::io_service& io_service, http::url const& url, std::string const& username, std::string const& password,
      wget_failure_callback failure, wget_success_callback success, wget_progress_callback progress)
   {
      if (url.scheme() == "http") {
         client_ptr c(new client(io_service, "GET", url, false, "", failure, success, progress));
         c->set_basic_auth("", username, password);
         c->start();
      } else {
         sclient_ptr c(new sclient(io_service, "GET", url, false, "", failure, success, progress));
         c->set_basic_auth("", username, password);
         c->start();
      }
   }

   void head(asio::io_service& io_service, http::url const& url, wget_failure_callback failure, wget_success_callback success,
      wget_progress_callback progress)
   {
      client_ptr c(new client(io_service, "HEAD", url, false, "", failure, success, progress));
      c->start();
   }

   void post(asio::io_service& io_service, http::url const& url, std::string const& data, wget_failure_callback failure,
      wget_success_callback success, wget_progress_callback progress)
   {
      client_ptr c(new client(io_service, "HEAD", url, true, data, failure, success, progress));
      c->start();
   }
   
   void propget(asio::io_service& io_service, http::url const& url, wget_failure_callback failure,
      wget_success_callback success, wget_progress_callback progress)
   {
      std::string data = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n<propfind xmlns=\"DAV:\">\n<prop>\n<getcontentlength xmlns=\"DAV:\"/>\n<getlastmodified xmlns=\"DAV:\"/>\n<executable xmlns=\"http://apache.org/dav/props/\"/>\n<resourcetype xmlns=\"DAV:\"/>\n<checked-in xmlns=\"DAV:\"/>\n<checked-out xmlns=\"DAV:\"/>\n</prop>\n</propfind>";
   
      if (url.scheme() == "http") {
         client_ptr c(new client(io_service, "PROPFIND", url, true, data, failure, success, progress));
         c->add_request_header("Depth", "1");
         c->add_request_header("Content-Type", "application/xml");
         c->start();
      } else {
         sclient_ptr c(new sclient(io_service, "PROPFIND", url, true, data, failure, success, progress));
         c->add_request_header("Depth", "1");
         c->add_request_header("Content-Type", "application/xml");
         c->start();
      }
   }

   void propget(asio::io_service& io_service, http::url const& url, std::string const& username, std::string const& password,
      wget_failure_callback failure, wget_success_callback success, wget_progress_callback progress)
   {
      std::string data = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n<propfind xmlns=\"DAV:\">\n<prop>\n<getcontentlength xmlns=\"DAV:\"/>\n<getlastmodified xmlns=\"DAV:\"/>\n<executable xmlns=\"http://apache.org/dav/props/\"/>\n<resourcetype xmlns=\"DAV:\"/>\n<checked-in xmlns=\"DAV:\"/>\n<checked-out xmlns=\"DAV:\"/>\n</prop>\n</propfind>";
   
      if (url.scheme() == "http") {
         client_ptr c(new client(io_service, "PROPFIND", url, true, data, failure, success, progress));
         c->add_request_header("Depth", "1");
         c->add_request_header("Content-Type", "application/xml");
         c->set_basic_auth("", username, password);
         c->start();
      } else {
         sclient_ptr c(new sclient(io_service, "PROPFIND", url, true, data, failure, success, progress));
         c->add_request_header("Depth", "1");
         c->add_request_header("Content-Type", "application/xml");
         c->set_basic_auth("", username, password);
         c->start();
      }
   }

}

//

#if defined(WGET_TEST)

void downloadFailure(const asio::error_code& error)
{
   std::cout << "*** Failure: " << error << std::endl;
}

void downloadProgress(size_t content_length, size_t read)
{
   std::cout << "*** Progress: read " << read << " out of " << content_length << std::endl;
}

void downloadSuccess(unsigned int status, std::map<std::string,std::string> const& headers, std::string& data)
{
   std::cout << "*** Success: status: " << status << std::endl;

#if 1
   std::cout << std::endl;
   for (std::map<std::string,std::string>::const_iterator i = headers.begin(); i != headers.end(); ++i) {
      std::cout << i->first << ": " << i->second << std::endl;
   }

   std::cout << std::endl;
   std::cout << data;
#endif

   //

   boost::regex url_regex("<D:href>(https://[^/]+/pyzor/updates/(\\d{10}\\d{10}))</D:href>", boost::regex::perl);
   
   std::string::const_iterator i = data.begin();
   std::string::const_iterator e = data.end();

   boost::smatch matches;
   while (boost::regex_search(i, e, matches, url_regex)) {
      std::cout << "Match: " << matches[1].str() << std::endl;

#if 0
      boost::smatch matches2;
      if (boost::regex_match(matches[1].str(), matches2, timestamp_regex)) {
         std::cout << matches2[1].str() << " -> " << matches2[2].str() << std::endl;
         boost::uint32_t from = boost::lexical_cast<boost::uint32_t>(matches2[1].str());
         boost::uint32_t to = boost::lexical_cast<boost::uint32_t>(matches2[2].str());
         updates_to_download_.push_back(matches[1].str());
      }
#endif

      i = matches[0].second;
   }   
}

int main(int argc, char** argv)
{
   std::cout << "*** Starting download for " << argv[1] << std::endl;
   
   asio::io_service io_service;
   http::propget(io_service, http::url(argv[1]), &downloadFailure, &downloadSuccess, &downloadProgress);
   io_service.run();

   return 0;
}

#endif
