// bohuno-setup.cpp

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <algorithm>
#include <fstream>
#include <string>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <asio.hpp>

#include <db.h>

#include "common.hpp"
#include "daemon.hpp"
#include "hash.hpp"
#include "license.hpp"
#include "packet.hpp"
#include "record.hpp"
#include "syslog.hpp"
#include "statistics.hpp"
#include "wget.hpp"

#include "bohuno-database.hpp"

// bohuno-pyzord-setup [-v] [-x] [-d database-directory] [-f snapshot-file] -k KEY

namespace bohuno {

   struct pyzord_setup_options
   {
      public:
      
         pyzord_setup_options()
            : verbose(false), debug(false), home("/var/lib/bohuno-pyzord"), user("bohuno")
         {
         }
      
      public:
      
         void usage()
         {
            std::cout << "usage: bohuno-pyzord-setup [-v] [-x] [-d db-home] [-s snapshot-file] [-u user] -k key" << std::endl;
         }
      
         bool parse(int argc, char** argv)
         {
            char c;
            while ((c = getopt(argc, argv, "hxvd:k:s:u:")) != EOF) {
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
                  case 's':
                     snapshot = optarg;
                     break;
                  case 'u': {
                     user = optarg;
                     break;
                  }
                  case 'k':
                     key = optarg;
                     break;
                  case 'h':
                  default:
                     usage();
                     return false;
               }
            }

            // Check if the specified user exists

            struct passwd* passwd = getpwnam(user.c_str());
            if (passwd == NULL) {
               std::cout << "bohuno-pyzord-setup: user '" << user << "' does not exist." << std::endl;
               return false;
            }
            uid = passwd->pw_uid;
            gid = passwd->pw_gid;

            // Check if the license is was specified

            if (key.empty()) {
               std::cout << "bohuno-pyzord-setup: the license key parameter (-k) is required" << std::endl;
               return false;
            }

            return true;
         }
         
      public:
         
         bool verbose;
         bool debug;
         boost::filesystem::path home;
         std::string key;
         boost::filesystem::path snapshot;
         std::string user;
         uid_t uid;
         gid_t gid;
   };

   ///

   asio::error_code gerror;
   int gstatus;
   std::string gcontent;

   void download_failure(const asio::error_code& error)
   {
      gerror = error;
   }

   size_t last_progress = 0;

   void download_progress(size_t content_length, size_t read)
   {
      if ((read - last_progress) > (8 * 1024 * 1024) || (content_length == read)) {
         last_progress = read;
         std::cout << " * Downloaded " << (int) ((double)read / (double)content_length * 100.0) << " %" << std::endl;
      }
   }

   void download_success(unsigned int status, std::map<std::string,std::string> const& headers, std::string& content)
   {
      gstatus = status;
      gcontent = content;
   }

   bool download(http::url const& url, std::string const& username, std::string const& password,
      asio::error_code& error, int& status, std::string& content)
   {
      std::cout << "Downloading " << url << " as " << username << "/" << password << std::endl;

      asio::io_service io_service;
      http::get(io_service, url, username, password, download_failure, download_success, download_progress);

      io_service.run();

      error = gerror;
      status = gstatus;
      content = gcontent;

      return !error;
   }

   void import_progress(size_t n)
   {
      std::cout << " * Imported " << n << " records." << std::endl;
   }

   int pyzord_setup_main(pyzord_setup_options& options)
   {
      try
      {
         // If we are not running as root then we need to run as the configured user. Otherwise
         // become the configured user.

         if (getuid() != 0) {
            if (getuid() != options.uid) {
               throw std::runtime_error(std::string("Should run as user '") + options.user
                  + std::string("' when not running as root"));
            }
         } else {
            if (setgid(options.gid) != 0) {
               throw std::runtime_error(std::string("Cannot change user: ") + strerror(errno));
            }
            if (setuid(options.uid) != 0) {
               throw std::runtime_error(std::string("Cannot change group: ") + strerror(errno));
            }
         }

         // Make sure our home is there

         if (!boost::filesystem::exists(options.home)) {
            throw std::runtime_error(std::string("Home directory ") + options.home.string() + std::string(" does not exist."));
         }         

         // Setup the database

         boost::filesystem::path db_home = options.home / "db";
         if (!boost::filesystem::exists(db_home)) {
            boost::filesystem::create_directory(db_home);
         }
         
         database db(db_home);

         // Check if the database has already been populated

         if (!db.empty()) {
            std::cout << "The database already contains data; please manually empty " << options.home
                      << " to reinitialize" << std::endl;
            exit(1);
         }

         // Load the license key
         
         bohuno::license license(options.key);
         std::cout << "Setting up pyzor database for " << license.realname() << std::endl;

         std::string content;
         
         if (options.snapshot.empty())
         {
            // Download the latest snapshot

            std::cout << "Downloading latest database snapshot. This can take a while." << std::endl;

            asio::error_code error;
            int status;

            http::url url("https://update.bohuno.com/pyzor/snapshots/current");
            if (!download(url, license.username(), license.password(), error, status, content)) {
               std::cout  << "Could not download Bohuno database snapshot: " << error.message() << std::endl;
               exit(1);
            }
         
            if (status != 200) {
               if (status == 401) {
                  std::cout << "Could not download Bohuno database snapshot: license key was rejected." << std::endl;
                  exit(1);
               } else {
                  std::cout << "Could not download Bohuno database snapshot: status " << status << std::endl;
                  exit(1);
               }
            }
         }
         else
         {
            std::ifstream file(options.snapshot.string().c_str(), std::ios::in | std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
               std::cout << "Cannot open " << options.snapshot.string() << std::endl;
               exit(1);
            }

            size_t size = file.tellg();
            content.resize(size);

            file.seekg(0, std::ios::beg);
            file.read((char*) &content[0], size);

            file.close();
         }
         
         // Insert into the database

         if (options.snapshot != "/dev/null")
         {
            std::cout << "Populating database" << std::endl;

            boost::iostreams::filtering_istream in;
            in.push(boost::iostreams::gzip_decompressor());
            in.push(boost::make_iterator_range(content));

            int n = db.import(in, import_progress);
            
            std::cout << "Database now contains " << n << " records." << std::endl;
         }
         
         // Write the license file
            
         boost::filesystem::path license_path = options.home / "license";
         std::ofstream license_file(license_path.string().c_str(), std::ios::out | std::ios::binary | std::ios::ate);
         if (!license_file.is_open()) {
            std::cout << "Cannot open " << license_path.string() << " for writing." << std::endl;
            exit(1);
         }

         license_file << options.key;

         license_file.close();
      }
      
      catch (std::exception const& e)
      {
         std::cerr << "Setup failed: " << e.what() << std::endl;
         exit(1);
      }
      
      return 0;
   }

}

int main(int argc, char** argv)
{
   bohuno::pyzord_setup_options options;
   if (options.parse(argc, argv)) {
      return bohuno::pyzord_setup_main(options);
   } else {
      return 1;
   }
}
