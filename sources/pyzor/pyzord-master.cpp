// pyzord.cc

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <asio.hpp>
#include <db.h>

#include "common.hpp"
#include "daemon.hpp"
#include "master.hpp"
#include "httpd.hpp"
#include "record.hpp"
#include "packet.hpp"
#include "syslog.hpp"
#include "statistics.hpp"

///

struct pyzor_master_options
{
   public:
      
      pyzor_master_options()
         : verbose(false), debug(false), user(NULL), cache(32), home("/var/lib/pyzor"), local("127.0.0.1"),
           uid(0), gid(0)
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzord-master [-v] [-x] [-u user] [-c cache-size] [-d database-dir] -l local-replica-address [-r remote-replica-adress]" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "hvxu:c:d:l:r:")) != EOF) {
            switch (c) {
               case 'v':
                  verbose = true;
                  break;
               case 'x':
                  debug = true;
                  break;
               case 'u': {
                  user = optarg;
                  struct passwd* passwd = getpwnam(optarg);
                  if (passwd == NULL) {
                     std::cout << "pyzor-slave: user '" << user << "' does not exist." << std::endl;
                     return false;
                  }
                  uid = passwd->pw_uid;
                  gid = passwd->pw_gid;
               }
                  break;
               case 'c':
                  cache = std::atoi(optarg);
                  break;
               case 'd':
                  home = optarg;
                  break;
               case 'l':
                  local = optarg;
                  break;
               case 'r':
                  slaves.push_back(optarg);
                  break;
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
      char* user;
      int cache;
      char* home;
      char* local;
      std::vector<std::string> slaves;

      uid_t uid;
      gid_t gid;
};

int pyzor_master_main(pyzor_master_options& options)
{
   pyzor::syslog syslog("pyzord-master", LOG_DAEMON, options.debug);
   syslog.notice() << "Starting pyzord-master on " << options.local << " with database home " << options.home;
   
   try {
      asio::io_service io_service;
      pyzor::master master(syslog, io_service, options.home, options.local, options.slaves, options.verbose);
      pyzor::run_in_thread(boost::bind(&pyzor::master::run, &master), boost::bind(&pyzor::master::stop, &master));
      syslog.notice() << "Server exited gracefully";
   } catch (std::exception& e) {
      syslog.error() << "Server exited with failure: " << e.what();
   }
   
   return 128;
}
   
int main(int argc, char** argv)
{
   pyzor_master_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         return pyzor_master_main(options);
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(pyzor_master_main, options), options.uid, options.gid);
      }
   } else {
      return 1;
   }
}
