

#include "httpd.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include <fstream>

namespace http {

   namespace server {

      namespace mime_types {

         struct mapping {
               const char* extension;
               const char* mime_type;
         } mappings[] = {
            { "gif", "image/gif" },
            { "htm", "text/html" },
            { "html", "text/html" },
            { "jpg", "image/jpeg" },
            { "png", "image/png" },
            { 0, 0 } // Marks end of list.
         };
         
         std::string extension_to_type(const std::string& extension)
         {
            for (mapping* m = mappings; m->extension; ++m) {
               if (m->extension == extension) {
                  return m->mime_type;
               }
            }
            
            return "text/plain";
         }
         
      } // namespace mime_types

      namespace status_strings {

         const std::string ok =
            "HTTP/1.0 200 OK\r\n";
         const std::string created =
            "HTTP/1.0 201 Created\r\n";
         const std::string accepted =
            "HTTP/1.0 202 Accepted\r\n";
         const std::string no_content =
            "HTTP/1.0 204 No Content\r\n";
         const std::string multiple_choices =
            "HTTP/1.0 300 Multiple Choices\r\n";
         const std::string moved_permanently =
            "HTTP/1.0 301 Moved Permanently\r\n";
         const std::string moved_temporarily =
            "HTTP/1.0 302 Moved Temporarily\r\n";
         const std::string not_modified =
            "HTTP/1.0 304 Not Modified\r\n";
         const std::string bad_request =
            "HTTP/1.0 400 Bad Request\r\n";
         const std::string unauthorized =
            "HTTP/1.0 401 Unauthorized\r\n";
         const std::string forbidden =
            "HTTP/1.0 403 Forbidden\r\n";
         const std::string not_found =
            "HTTP/1.0 404 Not Found\r\n";
         const std::string internal_server_error =
            "HTTP/1.0 500 Internal Server Error\r\n";
         const std::string not_implemented =
            "HTTP/1.0 501 Not Implemented\r\n";
         const std::string bad_gateway =
            "HTTP/1.0 502 Bad Gateway\r\n";
         const std::string service_unavailable =
            "HTTP/1.0 503 Service Unavailable\r\n";

         asio::const_buffer to_buffer(reply::status_type status)
         {
            switch (status)
            {
               case reply::ok:
                  return asio::buffer(ok);
               case reply::created:
                  return asio::buffer(created);
               case reply::accepted:
                  return asio::buffer(accepted);
               case reply::no_content:
                  return asio::buffer(no_content);
               case reply::multiple_choices:
                  return asio::buffer(multiple_choices);
               case reply::moved_permanently:
                  return asio::buffer(moved_permanently);
               case reply::moved_temporarily:
                  return asio::buffer(moved_temporarily);
               case reply::not_modified:
                  return asio::buffer(not_modified);
               case reply::bad_request:
                  return asio::buffer(bad_request);
               case reply::unauthorized:
                  return asio::buffer(unauthorized);
               case reply::forbidden:
                  return asio::buffer(forbidden);
               case reply::not_found:
                  return asio::buffer(not_found);
               case reply::internal_server_error:
                  return asio::buffer(internal_server_error);
               case reply::not_implemented:
                  return asio::buffer(not_implemented);
               case reply::bad_gateway:
                  return asio::buffer(bad_gateway);
               case reply::service_unavailable:
                  return asio::buffer(service_unavailable);
               default:
                  return asio::buffer(internal_server_error);
            }
         }

      } // namespace status_strings

      namespace misc_strings {

         const char name_value_separator[] = { ':', ' ' };
         const char crlf[] = { '\r', '\n' };

      } // namespace misc_strings

      std::vector<asio::const_buffer> reply::to_buffers()
      {
         std::vector<asio::const_buffer> buffers;
         buffers.push_back(status_strings::to_buffer(status));
         for (std::size_t i = 0; i < headers.size(); ++i)
         {
            header& h = headers[i];
            buffers.push_back(asio::buffer(h.name));
            buffers.push_back(asio::buffer(misc_strings::name_value_separator));
            buffers.push_back(asio::buffer(h.value));
            buffers.push_back(asio::buffer(misc_strings::crlf));
         }
         buffers.push_back(asio::buffer(misc_strings::crlf));
         buffers.push_back(asio::buffer(content));
         return buffers;
      }

      namespace stock_replies {

         const char ok[] = "";
         const char created[] =
            "<html>"
            "<head><title>Created</title></head>"
            "<body><h1>201 Created</h1></body>"
            "</html>";
         const char accepted[] =
            "<html>"
            "<head><title>Accepted</title></head>"
            "<body><h1>202 Accepted</h1></body>"
            "</html>";
         const char no_content[] =
            "<html>"
            "<head><title>No Content</title></head>"
            "<body><h1>204 Content</h1></body>"
            "</html>";
         const char multiple_choices[] =
            "<html>"
            "<head><title>Multiple Choices</title></head>"
            "<body><h1>300 Multiple Choices</h1></body>"
            "</html>";
         const char moved_permanently[] =
            "<html>"
            "<head><title>Moved Permanently</title></head>"
            "<body><h1>301 Moved Permanently</h1></body>"
            "</html>";
         const char moved_temporarily[] =
            "<html>"
            "<head><title>Moved Temporarily</title></head>"
            "<body><h1>302 Moved Temporarily</h1></body>"
            "</html>";
         const char not_modified[] =
            "<html>"
            "<head><title>Not Modified</title></head>"
            "<body><h1>304 Not Modified</h1></body>"
            "</html>";
         const char bad_request[] =
            "<html>"
            "<head><title>Bad Request</title></head>"
            "<body><h1>400 Bad Request</h1></body>"
            "</html>";
         const char unauthorized[] =
            "<html>"
            "<head><title>Unauthorized</title></head>"
            "<body><h1>401 Unauthorized</h1></body>"
            "</html>";
         const char forbidden[] =
            "<html>"
            "<head><title>Forbidden</title></head>"
            "<body><h1>403 Forbidden</h1></body>"
            "</html>";
         const char not_found[] =
            "<html>"
            "<head><title>Not Found</title></head>"
            "<body><h1>404 Not Found</h1></body>"
            "</html>";
         const char internal_server_error[] =
            "<html>"
            "<head><title>Internal Server Error</title></head>"
            "<body><h1>500 Internal Server Error</h1></body>"
            "</html>";
         const char not_implemented[] =
            "<html>"
            "<head><title>Not Implemented</title></head>"
            "<body><h1>501 Not Implemented</h1></body>"
            "</html>";
         const char bad_gateway[] =
            "<html>"
            "<head><title>Bad Gateway</title></head>"
            "<body><h1>502 Bad Gateway</h1></body>"
            "</html>";
         const char service_unavailable[] =
            "<html>"
            "<head><title>Service Unavailable</title></head>"
            "<body><h1>503 Service Unavailable</h1></body>"
            "</html>";

         std::string to_string(reply::status_type status)
         {
            switch (status)
            {
               case reply::ok:
                  return ok;
               case reply::created:
                  return created;
               case reply::accepted:
                  return accepted;
               case reply::no_content:
                  return no_content;
               case reply::multiple_choices:
                  return multiple_choices;
               case reply::moved_permanently:
                  return moved_permanently;
               case reply::moved_temporarily:
                  return moved_temporarily;
               case reply::not_modified:
                  return not_modified;
               case reply::bad_request:
                  return bad_request;
               case reply::unauthorized:
                  return unauthorized;
               case reply::forbidden:
                  return forbidden;
               case reply::not_found:
                  return not_found;
               case reply::internal_server_error:
                  return internal_server_error;
               case reply::not_implemented:
                  return not_implemented;
               case reply::bad_gateway:
                  return bad_gateway;
               case reply::service_unavailable:
                  return service_unavailable;
               default:
                  return internal_server_error;
            }
         }

      } // namespace stock_replies

      // Reply

      void reply::set_header(std::string const& name, std::string const& value)
      {
         for (size_t i = 0; i < headers.size(); i++) {
            if (headers[i].name == name) {
               headers[i].value = value;
               return;
            }
         }

         header h;
         h.name = name;
         h.value = value;
         
         headers.push_back(h);
      }

      reply reply::stock_reply(reply::status_type status)
      {
         reply rep;
         rep.status = status;
         rep.content = stock_replies::to_string(status);
         rep.headers.resize(2);
         rep.headers[0].name = "Content-Length";
         rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
         rep.headers[1].name = "Content-Type";
         rep.headers[1].value = "text/html";
         return rep;
      }      

      // Request

      bool request::has_header(std::string const& name) const
      {
         for (size_t i = 0; i < headers.size(); i++) {
            if (headers[i].name == name) {
               return true;
            }
         }
         return false;
      }

      std::string const& request::get_header(std::string const& name) const
      {
         for (size_t i = 0; i < headers.size(); i++) {
            if (headers[i].name == name) {
               return headers[i].value;
            }
         }
         throw std::runtime_error("Cannot find header");
      }

      // Response
      
      response::response()
         : committed_(false), status_(response::status_ok)
      {
         headers_["Content-Type"] = "text/html";
         headers_["Server"] = "Asio 0.3.9 / Embedded HTTP Server 0.0.1";
      }

      std::map<std::string,std::string> const& response::headers() const
      {
         return headers_;
      }
      
      bool response::has_header(std::string const& name) const
      {
         return (headers_.find(name) != headers_.end());
      }

      void response::header(std::string const& name, std::string const& value)
      {
         headers_[name] = value;
      }

      std::string const& response::header(std::string const& name) const
      {
         std::map<std::string,std::string>::const_iterator i = headers_.find("Content-Type");
         if (i != headers_.end()) {
            return i->second;
         } else {
            throw std::runtime_error("Cannot find header Content-Type");
         }
      }

      void response::status(response::status_code sc)
      {
         status_ = sc;
      }

      response::status_code response::status() const
      {
         return status_;
      }

      void response::content_type(std::string const& type)
      {
         headers_["Content-Type"] = type;
      }

      std::string const& response::content_type() const
      {
         std::map<std::string,std::string>::const_iterator i = headers_.find("Content-Type");
         if (i != headers_.end()) {
            return i->second;
         } else {
            throw std::runtime_error("Cannot find header Content-Type");
         }
      }

      void response::error(response::status_code sc)
      {
         if (committed_) {
            throw std::runtime_error("Response is already committed");
         }
         
         //reset();
         status(sc);
         commit();
      }

      void response::error(response::status_code sc, std::string const& message)
      {
         if (committed_) {
            throw std::runtime_error("Response is already committed");
         }

         //reset();
         status(sc);
         commit();
      }
            
      void response::redirect(std::string const& location)
      {
         if (committed_) {
            throw std::runtime_error("Response is already committed");
         }

         //reset();
         status(response::status_moved_permanently);
         header("Location", location);
         commit();
      }

      void response::write(std::string const& data)
      {
         if (committed_) {
            throw std::runtime_error("Response is already committed");
         }

         content_.append(data);
      }

      void response::commit()
      {
         if (committed_) {
            throw std::runtime_error("Response is already committed");
         } else {
            committed_ = true;
         }
      }

      std::vector<asio::const_buffer> response::to_buffers()
      {
         // Calculate the complete content size

         headers_["Content-Length"] = boost::lexical_cast<std::string>(content_.length());

         // Put together the response

         std::vector<asio::const_buffer> buffers;
         
         status_line_  = "HTTP/1.0 ";
         status_line_ += boost::lexical_cast<std::string>(status_);
         status_line_ += " Some Reason\r\n";
         
         buffers.push_back(asio::buffer(status_line_));
         
         for (std::map<std::string,std::string>::const_iterator i = headers_.begin(); i != headers_.end(); ++i) {
            buffers.push_back(asio::buffer(i->first));
            buffers.push_back(asio::buffer(misc_strings::name_value_separator));
            buffers.push_back(asio::buffer(i->second));
            buffers.push_back(asio::buffer(misc_strings::crlf));            
         }

         buffers.push_back(asio::buffer(misc_strings::crlf));
         
         // Add the content buffers
         
         buffers.push_back(asio::buffer(content_));

         return buffers;         
      }
      
      // Request Parser

      request_parser::request_parser()
         : state_(method_start)
      {
      }

      void request_parser::reset()
      {
         state_ = method_start;
      }

      boost::tribool request_parser::consume(request& req, char input)
      {
         switch (state_)
         {
            case method_start:
               if (!is_char(input) || is_ctl(input) || is_tspecial(input))
               {
                  return false;
               }
               else
               {
                  state_ = method;
                  req.method.push_back(input);
                  return boost::indeterminate;
               }
            case method:
               if (input == ' ')
               {
                  state_ = uri;
                  return boost::indeterminate;
               }
               else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
               {
                  return false;
               }
               else
               {
                  req.method.push_back(input);
                  return boost::indeterminate;
               }
            case uri_start:
               if (is_ctl(input))
               {
                  return false;
               }
               else
               {
                  state_ = uri;
                  req.uri.push_back(input);
                  return boost::indeterminate;
               }
            case uri:
               if (input == ' ')
               {
                  state_ = http_version_h;
                  return boost::indeterminate;
               }
               else if (is_ctl(input))
               {
                  return false;
               }
               else
               {
                  req.uri.push_back(input);
                  return boost::indeterminate;
               }
            case http_version_h:
               if (input == 'H')
               {
                  state_ = http_version_t_1;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_t_1:
               if (input == 'T')
               {
                  state_ = http_version_t_2;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_t_2:
               if (input == 'T')
               {
                  state_ = http_version_p;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_p:
               if (input == 'P')
               {
                  state_ = http_version_slash;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_slash:
               if (input == '/')
               {
                  req.http_version_major = 0;
                  req.http_version_minor = 0;
                  state_ = http_version_major_start;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_major_start:
               if (is_digit(input))
               {
                  req.http_version_major = req.http_version_major * 10 + input - '0';
                  state_ = http_version_major;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_major:
               if (input == '.')
               {
                  state_ = http_version_minor_start;
                  return boost::indeterminate;
               }
               else if (is_digit(input))
               {
                  req.http_version_major = req.http_version_major * 10 + input - '0';
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_minor_start:
               if (is_digit(input))
               {
                  req.http_version_minor = req.http_version_minor * 10 + input - '0';
                  state_ = http_version_minor;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case http_version_minor:
               if (input == '\r')
               {
                  state_ = expecting_newline_1;
                  return boost::indeterminate;
               }
               else if (is_digit(input))
               {
                  req.http_version_minor = req.http_version_minor * 10 + input - '0';
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case expecting_newline_1:
               if (input == '\n')
               {
                  state_ = header_line_start;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case header_line_start:
               if (input == '\r')
               {
                  state_ = expecting_newline_3;
                  return boost::indeterminate;
               }
               else if (!req.headers.empty() && (input == ' ' || input == '\t'))
               {
                  state_ = header_lws;
                  return boost::indeterminate;
               }
               else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
               {
                  return false;
               }
               else
               {
                  req.headers.push_back(header());
                  req.headers.back().name.push_back(input);
                  state_ = header_name;
                  return boost::indeterminate;
               }
            case header_lws:
               if (input == '\r')
               {
                  state_ = expecting_newline_2;
                  return boost::indeterminate;
               }
               else if (input == ' ' || input == '\t')
               {
                  return boost::indeterminate;
               }
               else if (is_ctl(input))
               {
                  return false;
               }
               else
               {
                  state_ = header_value;
                  req.headers.back().value.push_back(input);
                  return boost::indeterminate;
               }
            case header_name:
               if (input == ':')
               {
                  state_ = space_before_header_value;
                  return boost::indeterminate;
               }
               else if (!is_char(input) || is_ctl(input) || is_tspecial(input))
               {
                  return false;
               }
               else
               {
                  req.headers.back().name.push_back(input);
                  return boost::indeterminate;
               }
            case space_before_header_value:
               if (input == ' ')
               {
                  state_ = header_value;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case header_value:
               if (input == '\r')
               {
                  state_ = expecting_newline_2;
                  return boost::indeterminate;
               }
               else if (is_ctl(input))
               {
                  return false;
               }
               else
               {
                  req.headers.back().value.push_back(input);
                  return boost::indeterminate;
               }
            case expecting_newline_2:
               if (input == '\n')
               {
                  state_ = header_line_start;
                  return boost::indeterminate;
               }
               else
               {
                  return false;
               }
            case expecting_newline_3:
               return (input == '\n');
            default:
               return false;
         }
      }

      bool request_parser::is_char(int c)
      {
         return c >= 0 && c <= 127;
      }

      bool request_parser::is_ctl(int c)
      {
         return c >= 0 && c <= 31 || c == 127;
      }

      bool request_parser::is_tspecial(int c)
      {
         switch (c)
         {
            case '(': case ')': case '<': case '>': case '@':
            case ',': case ';': case ':': case '\\': case '"':
            case '/': case '[': case ']': case '?': case '=':
            case '{': case '}': case ' ': case '\t':
               return true;
            default:
               return false;
         }
      }

      bool request_parser::is_digit(int c)
      {
         return c >= '0' && c <= '9';
      }      
      
      // Connection

      connection::connection(asio::io_service& io_service, connection_manager& manager, std::vector<request_handler_ptr> const& request_handlers, std::map<std::string,request_handler_function>& request_handler_functions)
         : socket_(io_service), connection_manager_(manager), request_handlers_(request_handlers), request_handler_functions_(request_handler_functions)
      {
      }

      asio::ip::tcp::socket& connection::socket()
      {
         return socket_;
      }
      
      void connection::start()
      {
         socket_.async_read_some(
            asio::buffer(buffer_),
            boost::bind(&connection::handle_read, shared_from_this(), asio::placeholders::error, asio::placeholders::bytes_transferred)
         );
      }

      void connection::stop()
      {
         socket_.close();
      }
      
      void connection::handle_read(const asio::error_code& e, std::size_t bytes_transferred)
      {
         if (!e)
         {
            boost::tribool result;
            boost::tie(result, boost::tuples::ignore) = request_parser_.parse(request_, buffer_.data(), buffer_.data() + bytes_transferred);
            
            if (result)
            {
               if (request_handler_functions_.find(request_.uri) != request_handler_functions_.end()) {
                  response r;
                  try {
                     request_handler_functions_[request_.uri](request_, r);
                  } catch (...) {
                     reply_ = reply::stock_reply(reply::internal_server_error);
                     asio::async_write(
                        socket_,
                        reply_.to_buffers(),
                        boost::bind(&connection::handle_write, shared_from_this(), asio::placeholders::error)
                     );
                     return;
                  }
                  asio::async_write(
                     socket_,
                     r.to_buffers(),
                     boost::bind(&connection::handle_write, shared_from_this(), asio::placeholders::error)
                  );
               } else {
                  bool handled = false;
                  for (std::size_t i = 0; i < request_handlers_.size() && handled == false; i++) {
                     handled = request_handlers_[i]->handle_request(request_, reply_);
                  }

                  if (handled == false) {
                     reply_ = reply::stock_reply(reply::not_found);
                  }

                  asio::async_write(
                     socket_,
                     reply_.to_buffers(),
                     boost::bind(&connection::handle_write, shared_from_this(), asio::placeholders::error)
                  );
               }
            }
            else if (!result)
            {
               reply_ = reply::stock_reply(reply::bad_request);
               asio::async_write(
                  socket_,
                  reply_.to_buffers(),
                  boost::bind(&connection::handle_write, shared_from_this(), asio::placeholders::error)
               );
            }
            else
            {
               socket_.async_read_some(
                  asio::buffer(buffer_),
                  boost::bind(&connection::handle_read, shared_from_this(), asio::placeholders::error, asio::placeholders::bytes_transferred)
               );
            }
         }
         else if (e != asio::error::operation_aborted)
         {
            connection_manager_.stop(shared_from_this());
         }
      }

      void connection::handle_write(const asio::error_code& e)
      {
         if (e != asio::error::operation_aborted) {
            connection_manager_.stop(shared_from_this());
         }
      }
      
      // Connection Manager

      void connection_manager::start(connection_ptr c)
      {
         connections_.insert(c);
         c->start();
      }

      void connection_manager::stop(connection_ptr c)
      {
         connections_.erase(c);
         c->stop();
      }

      void connection_manager::stop_all()
      {
         std::for_each(connections_.begin(), connections_.end(), boost::bind(&connection::stop, _1));
         connections_.clear();
      }

      // Server

      server::server(asio::io_service& io_service, const std::string& address, const std::string& port)
         : io_service_(io_service), acceptor_(io_service_), connection_manager_(),
           new_connection_(new connection(io_service_, connection_manager_, request_handlers_, request_handler_functions_))
      {
         // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
         asio::ip::tcp::resolver resolver(io_service_);
         asio::ip::tcp::resolver::query query(address, port);
         asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
         acceptor_.open(endpoint.protocol());
         acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
         acceptor_.bind(endpoint);
         acceptor_.listen();
         acceptor_.async_accept(new_connection_->socket(), boost::bind(&server::handle_accept, this, asio::placeholders::error));
      }

      server::server(asio::io_service& io_service, const std::string& address, unsigned short port)
         : io_service_(io_service), acceptor_(io_service_), connection_manager_(),
           new_connection_(new connection(io_service_, connection_manager_, request_handlers_, request_handler_functions_))
      {
         // Open the acceptor with the option to reuse the address (i.e. SO_REUSEADDR).
         asio::ip::tcp::resolver resolver(io_service_);
         asio::ip::tcp::resolver::query query(address);
         asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
         endpoint.port(port);
         acceptor_.open(endpoint.protocol());
         acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
         acceptor_.bind(endpoint);
         acceptor_.listen();
         acceptor_.async_accept(new_connection_->socket(), boost::bind(&server::handle_accept, this, asio::placeholders::error));
      }
      
      void server::run()
      {
         // The io_service::run() call will block until all asynchronous operations
         // have finished. While the server is running, there is always at least one
         // asynchronous operation outstanding: the asynchronous accept call waiting
         // for new incoming connections.
         io_service_.run();
      }
      
      void server::stop()
      {
         // Post a call to the stop function so that server::stop() is safe to call
         // from any thread.
         io_service_.post(boost::bind(&server::handle_stop, this));
      }
      
      void server::handle_accept(const asio::error_code& e)
      {
         if (!e) {
            connection_manager_.start(new_connection_);
            new_connection_.reset(new connection(io_service_, connection_manager_, request_handlers_, request_handler_functions_));
            acceptor_.async_accept(new_connection_->socket(), boost::bind(&server::handle_accept, this, asio::placeholders::error));
         }
      }
      
      void server::handle_stop()
      {
         // The server is stopped by cancelling all outstanding asynchronous
         // operations. Once all operations have finished the io_service::run() call
         // will exit.
         acceptor_.close();
         connection_manager_.stop_all();
         io_service_.stop();
      }

      void server::add_request_handler(request_handler_ptr request_handler)
      {
         request_handlers_.push_back(request_handler);
      }

      void server::register_request_handler(std::string const& pattern, request_handler_function function)
      {
         request_handler_functions_[pattern] = function;
      }
      
   } // namespace server
   
} // namespace http
