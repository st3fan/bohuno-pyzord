// bohuno-updated.cpp

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>
#include <boost/timer.hpp>
#include <boost/tokenizer.hpp>

#include "common.hpp"
#include "daemon.hpp"
#include "database.hpp"
#include "httpd.hpp"
#include "record.hpp"
#include "packet.hpp"
#include "syslog.hpp"
#include "statistics.hpp"
#include "base64.hpp"

///

namespace bohuno {

   ///

   class updated : boost::noncopyable
   {
      public:

         static const int TIMER_DELAY = 5;
         static const int TIMER_INTERVAL = (5 * 60);

         static const boost::uint32_t SNAPSHOT_INTERVAL = (4 * 60 * 60);
         static const boost::uint32_t UPDATE_INTERVAL = (5 * 60);
         
      public:
         
         updated(pyzor::syslog& syslog, asio::io_service& io_service, pyzor::database& db, boost::filesystem::path const& root)
            : syslog_(syslog), io_service_(io_service), db_(db), root_(root), snapshot_timer_(io_service_),
              snapshots_directory_(root / "snapshots"), updates_directory_(root / "updates")
         {
            this->schedule_snapshot(TIMER_DELAY);
         }

      private:

         bool is_timestamp(std::string const& s)
         {
            if (s.length() == 10) {
               for (int i = 0; i < 10; i++) {
                  if (!std::isdigit(s[i])) {
                     return false;
                  }
               }
               return true;
            }
            return false;
         }
         
         bool parse_snapshot_file_name(boost::filesystem::path const& path, boost::uint32_t& timestamp)
         {
            if (boost::filesystem::is_regular(path) && is_timestamp(path.leaf())) {
               timestamp = boost::lexical_cast<boost::uint32_t>(path.leaf());
               return true;
            }
            return false;
         }
         
         bool find_most_recent_snapshot(boost::filesystem::path const& snapshot_directory, boost::filesystem::path& snapshot_path,
            boost::uint32_t& snapshot_timestamp)
         {
            boost::filesystem::path most_recent_path;
            boost::uint32_t most_recent_timestamp = 0;
            
            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(snapshot_directory); itr != end_itr; ++itr) {
               boost::uint32_t timestamp;
               if (parse_snapshot_file_name(itr->path(), timestamp)) {
                  if (timestamp > most_recent_timestamp) {
                     most_recent_timestamp = timestamp;
                     most_recent_path = itr->path();
                  }
               }
            }
            
            if (most_recent_timestamp != 0) {
               snapshot_path = most_recent_path;
               snapshot_timestamp = most_recent_timestamp;
               return true;
            }
            
            return false;
         }

         bool find_oldest_snapshot(boost::filesystem::path const& snapshot_directory, boost::filesystem::path& snapshot_path,
                                   boost::uint32_t& snapshot_timestamp)
         {
            boost::filesystem::path oldest_path;
            boost::uint32_t oldest_timestamp = 0xffffffff;
            
            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(snapshot_directory); itr != end_itr; ++itr) {
               boost::uint32_t timestamp;
               if (parse_snapshot_file_name(itr->path(), timestamp)) {
                  if (timestamp < oldest_timestamp) {
                     oldest_timestamp = timestamp;
                     oldest_path = itr->path();
                  }
               }
            }
            
            if (oldest_timestamp != 0xffffffff) {
               snapshot_path = oldest_path;
               snapshot_timestamp = oldest_timestamp;
               return true;
            }
            
            return false;
         }

         bool parse_update_file_name(std::string const& name, boost::uint32_t& min, boost::uint32_t& max)
         {
            if (name.length() == 20)
            {
               std::string mins = name.substr(0, 10);
               std::string maxs = name.substr(10, 10);
               
               if (is_timestamp(mins) && is_timestamp(maxs))
               {
                  min = boost::lexical_cast<boost::uint32_t>(mins);
                  max = boost::lexical_cast<boost::uint32_t>(maxs);
                  
                  return true;
               }      
            }
            
            return false;
         }

         bool find_most_recent_update(boost::filesystem::path const& directory, boost::filesystem::path& path, boost::uint32_t& min,
            boost::uint32_t& max)
         {
            boost::filesystem::path most_recent_path;
            boost::uint32_t most_recent_min = 0;
            boost::uint32_t most_recent_max = 0;
            
            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(directory); itr != end_itr; ++itr) {
               boost::uint32_t update_min, update_max;
               if (parse_update_file_name(itr->path().leaf(), update_min, update_max)) {
                  if (update_max > most_recent_max) {
                     most_recent_min = update_min;
                     most_recent_max = update_max;
                     most_recent_path = itr->path();
                  }
               }
            }
            
            if (most_recent_max != 0) {
               path = most_recent_path;
               min = most_recent_min;
               max = most_recent_max;
               return true;
            }
            
            return false;
         }
         
         //

         bool make_snapshot(time_t current_time)
         {
            bool created = false;

            // Find the last snapshot made
               
            boost::filesystem::path most_recent_snapshot_path;
            boost::uint32_t most_recent_snapshot_timestamp = 0;   
            find_most_recent_snapshot(snapshots_directory_, most_recent_snapshot_path, most_recent_snapshot_timestamp);
               
            // Create a new snapshot if the last one is older than SNAPSHOT_INTERVAL. Otherwise just create an update.
            
            if ((current_time - most_recent_snapshot_timestamp) >= SNAPSHOT_INTERVAL)
            {
               time_t timestamp = current_time - 1;
                  
               boost::filesystem::path current_snapshot_path(snapshots_directory_ / "current");
               boost::filesystem::path new_snapshot_path(snapshots_directory_ / boost::lexical_cast<std::string>(timestamp));
               boost::filesystem::path tmp_snapshot_path(new_snapshot_path.string() + ".tmp");
                  
               syslog_.notice() << "Creating snapshot to " << new_snapshot_path.string();
                  
               try {
                  boost::timer timer;
                  size_t n = db_.dump_modified_records(tmp_snapshot_path, 0, timestamp);
                  boost::filesystem::rename(tmp_snapshot_path, new_snapshot_path);
                  if (boost::filesystem::exists(current_snapshot_path)) {
                     boost::filesystem::remove(current_snapshot_path);
                  }
                  boost::filesystem::create_hard_link(new_snapshot_path, current_snapshot_path);
                  syslog_.notice() << "Snapshot succesfully created. Wrote " << (unsigned int) n << " records to "
                                   << new_snapshot_path.string() << " in " << timer.elapsed() << " seconds.";
                  created = true;
               } catch (std::exception& e) {
                  syslog_.error() << "Failed to make database snapshot: " << e.what();
               }
            }

            return created;
         }

         void make_update(time_t current_time)
         {
            // Find the date of the last update made
               
            boost::filesystem::path most_recent_update_path;
            boost::uint32_t most_recent_update_start = 0;
            boost::uint32_t most_recent_update_end = 0;

            if (!find_most_recent_update(updates_directory_, most_recent_update_path, most_recent_update_start, most_recent_update_end))
            {
               // If there are no updates then we find the timestamp of the last snapshot
               
               boost::filesystem::path most_recent_snapshot_path;
               boost::uint32_t most_recent_snapshot_timestamp = 0;
               
               if (find_most_recent_snapshot(snapshots_directory_, most_recent_snapshot_path, most_recent_snapshot_timestamp)) {
                  most_recent_update_end = most_recent_snapshot_timestamp;
               }                  
            }
               
            // If we found either an update or a snapshot then we make an update

            if (most_recent_update_end != 0)
            {
               time_t update_start = most_recent_update_end;
               time_t update_end = current_time - 1;

               std::string update_name
                  = boost::lexical_cast<std::string>(update_start) + boost::lexical_cast<std::string>(update_end);

               boost::filesystem::path new_update_path(updates_directory_ / update_name);
               boost::filesystem::path tmp_update_path(new_update_path.string() + ".tmp");

               syslog_.notice() << "Creating update " << new_update_path.string();
               
               try {
                  boost::timer timer;
                  size_t n = db_.dump_modified_records2(tmp_update_path, update_start, update_end);
                  boost::filesystem::rename(tmp_update_path, new_update_path);
                  syslog_.notice() << "Update succesfully created. Wrote " << (unsigned int) n << " records to "
                                   << new_update_path.string() << " in " << timer.elapsed() << " seconds.";
               } catch (std::exception& e) {
                  syslog_.error() << "Failed to make database update: " << e.what();
               }
            }
         }

         bool expire_snapshots()
         {
            // Delete snapshots that were made more than 8 hours ago. We always keep two recent ones around.

            int removed = 0;

            boost::uint32_t expire_time = time(NULL) - (8 * 60 * 60) - (2 * 60 * 60); // TODO Two extra hours to deal with clock skew

            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(snapshots_directory_); itr != end_itr; ++itr) {
               boost::uint32_t timestamp;
               if (parse_snapshot_file_name(itr->path(), timestamp)) {
                  if (timestamp < expire_time) {
                     syslog_.debug() << "Expiring snapshot " << itr->path().string();
                     boost::filesystem::remove(itr->path());
                     removed++;
                  }
               }
            }

            return (removed > 0);
         }

         void expire_updates()
         {
            // Delete updates that are older than the oldest snapshot time

            boost::filesystem::path oldest_snapshot_path;
            boost::uint32_t oldest_snapshot_timestamp = 0;
            
            if (find_oldest_snapshot(snapshots_directory_, oldest_snapshot_path, oldest_snapshot_timestamp)) {
               boost::filesystem::directory_iterator end_itr;
               for (boost::filesystem::directory_iterator itr(updates_directory_); itr != end_itr; ++itr) {
                  boost::uint32_t update_min, update_max;
                  if (parse_update_file_name(itr->path().leaf(), update_min, update_max)) {
                     if (update_max < oldest_snapshot_timestamp) {
                        syslog_.debug() << "Expiring update " << itr->path().string();
                        boost::filesystem::remove(itr->path());
                     }
                  }
               }
            }
         }
         
         void schedule_snapshot(int interval)
         {
            snapshot_timer_.expires_from_now(boost::posix_time::seconds(interval));
            snapshot_timer_.async_wait(boost::bind(&updated::snapshot, this, asio::placeholders::error));
         }

         void snapshot(const asio::error_code& error)
         {
            if (!error)
            {
               if (!db_.up()) {
                  // If the database is not up then retry in 5 seconds
                  this->schedule_snapshot(5);
               } else {
                  time_t current_time = time(NULL);            
                  // Expire
                  if (this->expire_snapshots()) {
                     this->expire_updates();
                  }
                  // Update
                  if (!this->make_snapshot(current_time)) {
                     this->make_update(current_time);
                  }               
                  this->schedule_snapshot(TIMER_INTERVAL - (time(NULL) - current_time));
               }
            }
         }

      public:

         void run()
         {
            io_service_.run();
         }

         void handle_stop()
         {
            snapshot_timer_.cancel();
            io_service_.stop();
         }

         void stop()
         {
            io_service_.post(boost::bind(&updated::handle_stop, this));
         }

      private:
         
         pyzor::syslog& syslog_;
         asio::io_service& io_service_;
         pyzor::database& db_;
         boost::filesystem::path root_;

         asio::deadline_timer snapshot_timer_;
         boost::filesystem::path snapshots_directory_;
         boost::filesystem::path updates_directory_;
   };
   
}

///

struct bohuno_updated_options
{
   public:
      
      bohuno_updated_options()
         : verbose(false), debug(false), home("/var/lib/pyzor"), user(NULL), root("/var/www/update.bohuno.com/pyzor"),
           uid(0), gid(0)
      {
      }
      
   public:
      
      void usage()
      {
         std::cout << "usage: bohuno-updated [-v] [-x] [-d db-home] [-r web-root] [-u user]" << std::endl;
      }
      
      bool parse(int argc, char** argv)
      {
         char c;
         while ((c = getopt(argc, argv, "hxvd:u:r:")) != EOF) {
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
               case 'u': {
                  user = optarg;
                  struct passwd* passwd = getpwnam(optarg);
                  if (passwd == NULL) {
                     std::cout << "bohuno-updated: user '" << user << "' does not exist." << std::endl;
                     return false;
                  }
                  uid = passwd->pw_uid;
                  gid = passwd->pw_gid;
                  break;
               }
               case 'r':
                  root = optarg;
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
      char* home;
      char* user;
      char* root;

      uid_t uid;
      gid_t gid;
};

///

int bohuno_updated_main(bohuno_updated_options& options)
{
   pyzor::syslog syslog("bohuno-updated", LOG_DAEMON, options.debug);
   
   syslog.notice() << "Starting bohuno-updated with database home " << options.home << " and web root " << options.root;

   try {
      asio::io_service io_service;
      pyzor::database db(syslog, io_service, options.home, options.verbose);
      bohuno::updated updated(syslog, io_service, db, options.root);
      
      {
         // Block all signals for background thread.
         sigset_t new_mask;
         sigfillset(&new_mask);
         sigset_t old_mask;
         pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

         // Run server in background thread.
         asio::thread t(boost::bind(&bohuno::updated::run, &updated));

         // Restore previous signals.
         pthread_sigmask(SIG_SETMASK, &old_mask, 0);

         // Wait for signal indicating time to shut down.
         sigset_t wait_mask;
         sigemptyset(&wait_mask);
         sigaddset(&wait_mask, SIGINT);
         sigaddset(&wait_mask, SIGQUIT);
         sigaddset(&wait_mask, SIGTERM);
         pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
         int sig = 0;
         sigwait(&wait_mask, &sig);

         syslog.notice() << "Received a signal, stopping the server.";

         // Stop the server.
         updated.stop();
         t.join();
      }
   } catch (std::exception& e) {
      syslog.error() << "Server exited with failure: " << e.what();
   }

   syslog.notice() << "Server exited gracefully.";

   return 0;
}

int main(int argc, char** argv)
{
   bohuno_updated_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         return bohuno_updated_main(options);
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(bohuno_updated_main, options), options.uid, options.gid);
      }
   } else {
      return 1;
   }
}
