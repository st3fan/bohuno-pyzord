// pyzord-slave.cc

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <asio.hpp>
#include <db.h>

#include "common.hpp"
#include "daemon.hpp"
#include "slave.hpp"
#include "record.hpp"
#include "packet.hpp"
#include "statistics.hpp"
#include "syslog.hpp"

void usage()
{
   std::cout << "usage: pyzord-slave [-v] [-x] [-u user] [-d database-home] [-c cache-size] -l local-replica-address -m master-replica-address -s other-slave-address" << std::endl;
}

struct pyzor_slave_options
{
   public:
      
      pyzor_slave_options()
         : verbose(false), debug(false), cache(32), local("127.0.0.1"), master(NULL), home("/var/lib/pyzor"),
           user(NULL), uid(0), gid(0)
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: pyzor-slave [-v] [-x] [-d db-home] [-c cache-size] [-l local-addres] [-u user] -m master [-s slave]" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "hxvd:u:m:l:c:")) != EOF) {
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
               case 'c':
                  cache = atoi(optarg);
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
                  break;
               }
               case 'm':
                  master = optarg;
                  break;
               case 's':
                  slaves.push_back(optarg);
                  break;
               case 'h':
               default:
                  usage();
                  return false;
            }
         }

         if (master == NULL) {
            usage();
            return false;
         }

         return true;
      }

   public:
      
      bool verbose;
      bool debug;
      int cache;
      char* local;
      char* master;
      char* home;
      char* user;
      uid_t uid;
      gid_t gid;
      std::vector<std::string> slaves;
};

int pyzor_slave_main(pyzor_slave_options& options)
{
   pyzor::syslog syslog("pyzor-slave", LOG_DAEMON, options.debug);

   syslog.notice() << "Starting pyzor-slave on " << options.local << " with database home " << options.home
                   << " and master " << options.master;

   try {
      asio::io_service io_service;
      pyzor::slave slave(syslog, io_service, options.home, options.cache, options.local, options.master,
         options.slaves, options.verbose);
      pyzor::run_in_thread(boost::bind(&pyzor::slave::run, &slave), boost::bind(&pyzor::slave::stop, &slave));
      syslog.notice() << "Server exited gracefully";
   } catch (std::exception& e) {
      syslog.error() << "Server exited with failure: " << e.what();
   }

   return 128;
}

int main(int argc, char** argv)
{
   pyzor_slave_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         return pyzor_slave_main(options);
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(pyzor_slave_main, options), options.uid, options.gid);
      }
   } else {
      return 1;
   }
}
