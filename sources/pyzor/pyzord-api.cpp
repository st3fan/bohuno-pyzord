// pyzord-api.cpp

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/tokenizer.hpp>

#include "common.hpp"
#include "daemon.hpp"
#include "database.hpp"
#include "httpd.hpp"
#include "record.hpp"
#include "packet.hpp"
#include "syslog.hpp"
#include "statistics.hpp"

///

struct api_error {
   public:
      api_error(std::string const& code, std::string const& message)
         : code_(code), message_(message)
      {
      }
   public:
      std::string to_json()
      {
         std::string json;
         json.append("{ ");
         json.append("\"Code\": \"");
         json.append(code_);
         json.append("\", \"Message\": \"");
         json.append(message_);
         json.append("\"");
         json.append(" }");
         return json;
      }
   public:
      std::string code_;
      std::string message_;
};

///
   
class api_request_handler : public http::server::request_handler
{
   public:
         
      api_request_handler(pyzor::database& db)
         : db_(db)
      {
      }
         
   public:
         
      virtual bool handle_request(const http::server::request& req, http::server::reply& rep)
      {
         if (!boost::algorithm::starts_with(req.uri, "/api?")) {
            return false;
         }
            
         std::string query = req.uri.substr(strlen("/api?"));
            
         std::map<std::string,std::string> parameters;
         if (!parse_query(query, parameters)) {
            rep = http::server::reply::stock_reply(http::server::reply::internal_server_error);
            return true;
         }

         // Check if the required parameters are there

         std::vector<api_error> errors;

         if (parameters.find("Action") == parameters.end()) {
            errors.push_back(api_error("MissingParameter", "The 'Action' parameter is required."));
         }

         if (parameters.find("Version") == parameters.end()) {
            errors.push_back(api_error("MissingParameter", "The 'Version' parameter is required."));
         }

         if (parameters.find("Hash") == parameters.end()) {
            errors.push_back(api_error("MissingParameter", "The 'Hash' parameter is required."));
         }

         // Check if the version is ok

         if (parameters.find("Version") != parameters.end() && parameters["Version"] != "2007-11-26") {
            errors.push_back(api_error("NoSuchVersion", "An incorrect version was specified. Current API version is 2007-11-26."));
         }

         if (!errors.empty()) {
            rep = error_reply(errors);
         } else {
            // Dispatch to the right handler         
            if (parameters["Action"] == "Get") {
               handle_get(req, rep, parameters);
            } else if (parameters["Action"] == "Report") {
               handle_report(req, rep, parameters);
            } else if (parameters["Action"] == "Whitelist") {
               handle_whitelist(req, rep, parameters);
            } else if (parameters["Action"] == "Delete") {
               handle_delete(req, rep, parameters);
            } else {
               // Invalid Action
               errors.push_back(api_error("InvalidAction", "The action specified is invalid."));
               rep = error_reply(errors);
            }
         }
         
         return true;
      }

   private:
      
      http::server::reply error_reply(std::vector<api_error>& errors)
      {
         http::server::reply rep;

         rep.status = http::server::reply::ok;
         
         rep.content.append("{ \n");
         rep.content.append("  \"Errors\": [\n");
         for (size_t i = 0; i < errors.size(); i++) {
            if (i > 0) {
               rep.content.append(", \n");
            }
            rep.content.append("    ");
            rep.content.append(errors[i].to_json());
            
         }
         rep.content.append("\n");
         rep.content.append("  ]\n");
         rep.content.append("}\n");
            
         rep.headers.resize(2);
         rep.headers[0].name = "Content-Length";
         rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
         rep.headers[1].name = "Content-Type";
         rep.headers[1].value = "text/plain";         

         return rep;
      }

      http::server::reply nil_reply()
      {
         http::server::reply rep;

         rep.status = http::server::reply::ok;
         
         rep.content.append("{ \n");
         rep.content.append("  \"Response\": nil\n");
         rep.content.append("}\n");
            
         rep.headers.resize(2);
         rep.headers[0].name = "Content-Length";
         rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
         rep.headers[1].name = "Content-Type";
         rep.headers[1].value = "text/plain";         

         return rep;
      }

      void handle_report(const http::server::request& req, http::server::reply& rep, std::map<std::string,std::string>& parameters)
      {
         db_.report(parameters["Hash"]);
         rep = nil_reply();
      }

      void handle_whitelist(const http::server::request& req, http::server::reply& rep, std::map<std::string,std::string>& parameters)
      {
         db_.whitelist(parameters["Hash"]);
         rep = nil_reply();
      }

      void handle_delete(const http::server::request& req, http::server::reply& rep, std::map<std::string,std::string>& parameters)
      {
         db_.erase(parameters["Hash"]);
         rep = nil_reply();
      }

      void handle_get(const http::server::request& req, http::server::reply& rep, std::map<std::string,std::string>& parameters)
      {
         pyzor::record r;
         if (db_.get(parameters["Hash"], r) == true) {
            // ...
         }
            
         // Fill out the reply to be sent to the client.
         
         rep.content.append("{ \n");
         rep.content.append("  \"Response\": {\n");
         rep.content.append("    \"Hash\": \""); rep.content.append(parameters["Hash"]); rep.content.append("\",\n");

         rep.content.append("    \"RecordEntered\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.entered())); rep.content.append(",\n");
         rep.content.append("    \"RecordUpdated\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.updated())); rep.content.append(",\n");

         rep.content.append("    \"WhitelistCount\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.whitelist_count())); rep.content.append(",\n");
         rep.content.append("    \"WhitelistEntered\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.whitelist_entered())); rep.content.append(",\n");
         rep.content.append("    \"WhitelistUpdated\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.whitelist_updated())); rep.content.append(",\n");
            
         rep.content.append("    \"ReportCount\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.report_count())); rep.content.append(",\n");
         rep.content.append("    \"ReportEntered\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.report_entered())); rep.content.append(",\n");
         rep.content.append("    \"ReportUpdated\": ");
            rep.content.append(boost::lexical_cast<std::string>(r.report_updated())); rep.content.append("\n");
         rep.content.append("  }\n");
         rep.content.append("}\n");
            
         rep.headers.resize(2);
         rep.headers[0].name = "Content-Length";
         rep.headers[0].value = boost::lexical_cast<std::string>(rep.content.size());
         rep.headers[1].name = "Content-Type";
         rep.headers[1].value = "text/plain";
      }

   private:
      
      bool parse_query(std::string const& query, std::map<std::string,std::string>& parameters)
      {
         boost::char_separator<char> separator("&");
         boost::tokenizer< boost::char_separator<char> > tokenizer(query, separator);
         
         for (boost::tokenizer< boost::char_separator<char> >::iterator i = tokenizer.begin(); i!=tokenizer.end(); ++i) {
            boost::regex regex("^([A-Za-z0-9-]+?)=([A-Za-z0-9-]+)$", boost::regex::perl);
            boost::smatch matches;
            if (boost::regex_match(*i, matches, regex)) {
               parameters[matches[1].str()] = matches[2].str();
            } else {
               return false;
            }
         }

         return true;
      }
         
   private:
         
      pyzor::database& db_;
};
      
typedef boost::shared_ptr<api_request_handler> api_request_handler_ptr;

///

struct pyzord_api_options
{
   public:
      
      pyzord_api_options()
         : verbose(false), debug(false), local("127.0.0.1"), port("8080"), home("/var/lib/pyzor"), user(NULL), uid(0), gid(0)
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzord-api [-v] [-x] [-d db-home] [-l http-addres] [-p http-port] [-u user]" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "hxvd:p:u:l:")) != EOF) {
            switch (c) {
               case 'x':
                  debug = true;
                  break;
               case 'v':
                  verbose = true;
                  break;
               case 'd':
                  home = optarg;
                  break;
               case 'l':
                  local = optarg;
                  break;
               case 'p':
                  port = optarg;
                  break;
               case 'u': {
                  user = optarg;
                  struct passwd* passwd = getpwnam(optarg);
                  if (passwd == NULL) {
                     std::cout << "pyzord-server: user '" << user << "' does not exist." << std::endl;
                     return false;
                  }
                  uid = passwd->pw_uid;
                  gid = passwd->pw_gid;
                  break;
               }
               case 'h':
               default:
                  usage();
                  return false;
            }
         }

         return true;
      }

   public:
      
      bool verbose;
      bool debug;
      char* local;
      char* port;
      char* home;
      char* user;
      uid_t uid;
      gid_t gid;
};

///
   
int pyzord_api_main(pyzord_api_options& options)
{
   pyzor::syslog syslog("pyzord-api", LOG_DAEMON, options.debug);
   syslog.notice() << "Starting pyzord-api on http://*:" << options.port << " with database home " << options.home;

   try {
      asio::io_service io_service;
      pyzor::database db(syslog, io_service, options.home, options.verbose);
      http::server::server server(io_service, options.local, options.port);
      server.add_request_handler(http::server::request_handler_ptr(new api_request_handler(db)));
      pyzor::run_in_thread(boost::bind(&http::server::server::run, &server),
         boost::bind(&http::server::server::stop, &server));
      syslog.notice() << "Server exited gracefully.";
   } catch (std::exception& e) {
      syslog.error() << "Server exited with failure: " << e.what();
   }

   return 0;
}

int main(int argc, char** argv)
{
   pyzord_api_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         return pyzord_api_main(options);
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(pyzord_api_main, options), options.uid, options.gid);
      }
   } else {
      return 1;
   }
}
