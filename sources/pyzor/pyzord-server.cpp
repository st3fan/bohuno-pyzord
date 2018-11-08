// pyzord.cc

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

#include <boost/bind.hpp>

#include "common.hpp"
#include "daemon.hpp"
#include "database.hpp"
#include "httpd.hpp"
#include "record.hpp"
#include "packet.hpp"
#include "server.hpp"
#include "syslog.hpp"
#include "statistics.hpp"

using asio::ip::udp;

struct pyzord_server_options
{
   public:
      
      pyzord_server_options()
         : verbose(false), debug(false), local("127.0.0.1"), port("24441"), home("/var/lib/pyzor"), user(NULL), uid(0), gid(0)
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzord-server [-v] [-x] [-u user] [-d database-dir] [-l pyzor-address] [-p pyzor-port] [-a admin-ip-address]" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "xhvd:u:p:l:a:")) != EOF) {
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
               case 'a':
                  admin_addresses.push_back(optarg);
                  break;
               case 'u': {
                  user = optarg;
                  struct passwd* passwd = getpwnam(user);
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
      std::vector<std::string> admin_addresses;
      uid_t uid;
      gid_t gid;
};

int pyzord_server_main(pyzord_server_options& options)
{
   pyzor::syslog syslog("pyzord-server", LOG_DAEMON, options.verbose);
   syslog.notice() << "Starting pyzord-server on *:" << options.port << " with database home " << options.home;

   try {
      asio::io_service io_service;      
      pyzor::database db(syslog, io_service, options.home, options.verbose);
      pyzor::server server(syslog, io_service, options.local, options.port, db, options.verbose);
      server.add_admin_address("127.0.0.1");
      for (size_t i = 0; i < options.admin_addresses.size(); i++) {
         server.add_admin_address(options.admin_addresses[i]);
      }
      pyzor::run_in_thread(boost::bind(&pyzor::server::run, &server), boost::bind(&pyzor::server::stop, &server));
   } catch (std::exception& e) {
      syslog.error() << "Server exited with failure: " << e.what();
   }

   syslog.notice() << "Server exited gracefully.";
   
   return 128;
}

int main(int argc, char** argv)
{
   pyzord_server_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         return pyzord_server_main(options);
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(pyzord_server_main, options), options.uid, options.gid);
      }
   } else {
      return 1;
   }
}
