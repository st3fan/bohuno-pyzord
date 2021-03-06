
#ifndef HTTPD_HPP
#define HTTPD_HPP

#include <asio.hpp>

#include <boost/array.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/tuple/tuple.hpp>

#include <set>

namespace http {

   namespace server {

      namespace mime_types {

         /// Convert a file extension into a MIME type.
         std::string extension_to_type(const std::string& extension);
         
      } // namespace mime_types

      struct header
      {
         public:
            std::string name;
            std::string value;
      };

      /// A reply to be sent to a client.
      struct reply
      {
         public:
            /// The status of the reply.
            enum status_type
            {
               ok = 200,
               created = 201,
               accepted = 202,
               no_content = 204,
               multiple_choices = 300,
               moved_permanently = 301,
               moved_temporarily = 302,
               not_modified = 304,
               bad_request = 400,
               unauthorized = 401,
               forbidden = 403,
               not_found = 404,
               internal_server_error = 500,
               not_implemented = 501,
               bad_gateway = 502,
               service_unavailable = 503
            } status;
            
            /// The headers to be included in the reply.
            std::vector<header> headers;

            void set_header(std::string const& name, std::string const& value);
            
            /// The content to be sent in the reply.
            std::string content;
            
            /// Convert the reply into a vector of buffers. The buffers do not own the
            /// underlying memory blocks, therefore the reply object must remain valid and
            /// not be changed until the write operation has completed.
            std::vector<asio::const_buffer> to_buffers();

            /// Get a stock reply.
            static reply stock_reply(status_type status);
      };      

      /// A request received from a client.
      
      struct request
      {
         public:
            
            std::string method;
            std::string uri;
            int http_version_major;
            int http_version_minor;
            std::vector<header> headers;
            
         public:
            
            bool has_header(std::string const& name) const;
            std::string const& get_header(std::string const& name) const;
      };

      // Response

      class response
      {
         public:

            enum status_code
            {
               status_ok = 200,
               status_created = 201,
               status_accepted = 202,
               status_no_content = 204,
               status_multiple_choices = 300,
               status_moved_permanently = 301,
               status_moved_temporarily = 302,
               status_not_modified = 304,
               status_bad_request = 400,
               status_unauthorized = 401,
               status_forbidden = 403,
               status_not_found = 404,
               status_internal_server_error = 500,
               status_not_implemented = 501,
               status_bad_gateway = 502,
               status_service_unavailable = 503
            };
            
         public:

            response();

         public:
            
            std::map<std::string,std::string> const& headers() const;

            bool has_header(std::string const& name) const;
            void header(std::string const& name, std::string const& value);
            std::string const& header(std::string const& name) const;

            void status(status_code sc);
            status_code status() const;
            
            void content_type(std::string const& type);
            std::string const& content_type() const;

            void error(status_code sc);
            void error(status_code sc, std::string const& message);
            
            void redirect(std::string const& location);

            std::ostream& ostream();

            void write(std::string const& data);
            void commit();

            template <typename T>
            response& operator << (T const& v)
            {
               content_.append(boost::lexical_cast<std::string>(v));
               return *this;
            }

         public:

            std::vector<asio::const_buffer> to_buffers();

         private:
            
            std::map<std::string,std::string> headers_;
            std::string content_;
            bool committed_;
            status_code status_;
            std::string status_line_;
      };

      typedef boost::shared_ptr<response> response_ptr;

      // Request Handler

      class request_handler : private boost::noncopyable
      {
         public:
            request_handler() {}
            virtual ~request_handler() {}
         public:
            /// Handle a request and produce a reply.
            virtual bool handle_request(const request& req, reply& rep) = 0;
      };

      typedef boost::shared_ptr<request_handler> request_handler_ptr;

      // Request Parser

      /// Parser for incoming requests.
      class request_parser
      {
         public:
            /// Construct ready to parse the request method.
            request_parser();

            /// Reset to initial parser state.
            void reset();

            /// Parse some data. The tribool return value is true when a complete request
            /// has been parsed, false if the data is invalid, indeterminate when more
            /// data is required. The InputIterator return value indicates how much of the
            /// input has been consumed.
            template <typename InputIterator>
            boost::tuple<boost::tribool, InputIterator> parse(request& req,
               InputIterator begin, InputIterator end)
            {
               while (begin != end)
               {
                  boost::tribool result = consume(req, *begin++);
                  if (result || !result)
                     return boost::make_tuple(result, begin);
               }
               boost::tribool result = boost::indeterminate;
               return boost::make_tuple(result, begin);
            }

         private:
            /// Handle the next character of input.
            boost::tribool consume(request& req, char input);

            /// Check if a byte is an HTTP character.
            static bool is_char(int c);

            /// Check if a byte is an HTTP control character.
            static bool is_ctl(int c);

            /// Check if a byte is defined as an HTTP tspecial character.
            static bool is_tspecial(int c);

            /// Check if a byte is a digit.
            static bool is_digit(int c);

            /// The current state of the parser.
            enum state
            {
               method_start,
               method,
               uri_start,
               uri,
               http_version_h,
               http_version_t_1,
               http_version_t_2,
               http_version_p,
               http_version_slash,
               http_version_major_start,
               http_version_major,
               http_version_minor_start,
               http_version_minor,
               expecting_newline_1,
               header_line_start,
               header_lws,
               header_name,
               space_before_header_value,
               header_value,
               expecting_newline_2,
               expecting_newline_3
            } state_;
      };      

      //

      typedef boost::function<void (const request& req, response& rep)> request_handler_function;
      
      // Connection

      class connection_manager;

      /// Represents a single connection from a client.
      class connection : public boost::enable_shared_from_this<connection>, private boost::noncopyable
      {
         public:
            /// Construct a connection with the given io_service.
            explicit connection(asio::io_service& io_service, connection_manager& manager, std::vector<request_handler_ptr> const& request_handlers, std::map<std::string,request_handler_function>& request_handler_functions);
            
            /// Get the socket associated with the connection.
            asio::ip::tcp::socket& socket();
            
            /// Start the first asynchronous operation for the connection.
            void start();
            
            /// Stop all asynchronous operations associated with the connection.
            void stop();
            
         private:
            /// Handle completion of a read operation.
            void handle_read(const asio::error_code& e, std::size_t bytes_transferred);
            
            /// Handle completion of a write operation.
            void handle_write(const asio::error_code& e);
            
            /// Socket for the connection.
            asio::ip::tcp::socket socket_;
            
            /// The manager for this connection.
            connection_manager& connection_manager_;
            
            /// The handlers used to process the incoming request.
            std::vector<request_handler_ptr> const& request_handlers_;

            std::map<std::string,request_handler_function>& request_handler_functions_;
            
            /// Buffer for incoming data.
            boost::array<char, 8192> buffer_;
            
            /// The incoming request.
            request request_;
            
            /// The parser for the incoming request.
            request_parser request_parser_;
            
            /// The reply to be sent back to the client.
            reply reply_;
      };
      
      typedef boost::shared_ptr<connection> connection_ptr;

      // Connection Manager

      /// Manages open connections so that they may be cleanly stopped when the server
      /// needs to shut down.
      class connection_manager : private boost::noncopyable
      {
         public:
            /// Add the specified connection to the manager and start it.
            void start(connection_ptr c);
            
            /// Stop the specified connection.
            void stop(connection_ptr c);
            
            /// Stop all connections.
            void stop_all();
            
         private:
            /// The managed connections.
            std::set<connection_ptr> connections_;
      };
      
      // Server

      /// The top-level class of the HTTP server.
      class server : private boost::noncopyable
      {
         public:
            /// Construct the server to listen on the specified TCP address and port, and
            /// serve up files from the given directory.
            explicit server(asio::io_service& io_service, const std::string& address, const std::string& port);

            /// Construct the server to listen on the specified TCP address and port, and
            /// serve up files from the given directory.
            explicit server(asio::io_service& io_service, const std::string& address, unsigned short port);
            
            /// Run the server's io_service loop.
            void run();
            
            /// Stop the server.
            void stop();

            /// Add a request handler.
            void add_request_handler(request_handler_ptr request_handler);

            /// Register a request handler function.
            void register_request_handler(std::string const& pattern, request_handler_function function);
            
         private:
            /// Handle completion of an asynchronous accept operation.
            void handle_accept(const asio::error_code& e);
            
            /// Handle a request to stop the server.
            void handle_stop();
            
            /// The io_service used to perform asynchronous operations.
            asio::io_service& io_service_;
            
            /// Acceptor used to listen for incoming connections.
            asio::ip::tcp::acceptor acceptor_;
            
            /// The connection manager which owns all live connections.
            connection_manager connection_manager_;
            
            /// The next connection to be accepted.
            connection_ptr new_connection_;
            
            /// The handlers for all incoming requests.
            std::vector<request_handler_ptr> request_handlers_;

            /// The handler functions by pattern
            std::map<std::string,request_handler_function> request_handler_functions_;
      };
      
   }
   
}

#endif
