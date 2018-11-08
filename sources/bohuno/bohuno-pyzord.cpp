// bohuno-pyzord.cpp

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <algorithm>
#include <string>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
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
#include "md5_filter.hpp"

#include "bohuno-database.hpp"

namespace bohuno {

   class pyzord : boost::noncopyable
   {
      public:

         pyzord(pyzor::syslog& syslog, asio::io_service& io_service, boost::filesystem::path const& home,
            std::string const& address, std::string const& port, bool verbose)
            : syslog_(syslog), io_service_(io_service),
              home_(home), address_(address), port_(port), verbose_(verbose),
              license_(home_ / "license"), database_(home_ / "db"),
              statistics_timer_(io_service), checkpoint_timer_(io_service), updates_scan_timer_(io_service),
              socket_(io_service), shutdown_(false),  download_in_progress_(false)
         {
            // Check if our home is there - Is actually already checked by license and database

            if (!boost::filesystem::exists(home_)) {
               throw std::runtime_error(std::string("Home directory") + home_.string() + std::string(" does not exist."));
            }

            // Only start if the database has been initialized
#if 0
            if (database_.empty()) {
               throw std::runtime_error("Database has not been initialized; please run setup.");
            }
#endif
            // Schedule the task that will upload statistics to the mothership

            this->schedule_statistics();

            // Schedule a periodic task to checkpoint the database
            
            this->schedule_checkpoint();

            // Schedule a periodic task that will collect a list of updates to download

            this->schedule_updates_scan(5);

            // Resolve the address and start receiving Pyzor requests
            
            asio::ip::udp::resolver resolver(io_service_);
            asio::ip::udp::resolver::query query(address_, port_);
            asio::ip::udp::resolver::iterator endpoint_iterator = resolver.resolve(query);
            asio::ip::udp::resolver::iterator end;
      
            if (endpoint_iterator == end) {
               throw std::runtime_error(std::string("Cannot resolve ") + address);
            }
      
            asio::ip::udp::endpoint endpoint = *endpoint_iterator;
      
            socket_.open(endpoint.protocol());
            socket_.bind(endpoint);

            socket_.async_receive_from(
               asio::buffer(data_, max_length),
               sender_endpoint_,
               boost::bind(&pyzord::handle_receive_from, this, asio::placeholders::error,
                  asio::placeholders::bytes_transferred)
            );
         }
         
      public:

         void run()
         {
            io_service_.run();
         }

         void handle_stop()
         {
            shutdown_ = true;
            socket_.close();
            checkpoint_timer_.cancel();
            updates_scan_timer_.cancel();
            statistics_timer_.cancel();
         }

         void stop()
         {
            io_service_.post(boost::bind(&pyzord::handle_stop, this));
         }

      private:

         void handle_statistics_failure(const asio::error_code& error)
         {
            this->schedule_statistics();
         }

         void handle_statistics_success(unsigned int status, std::map<std::string,std::string> const& headers,
            std::string& data)
         {
            this->schedule_statistics();
         }

         std::string get_computer_id()
         {
            std::string id("unknown");
            
            if (boost::filesystem::exists("/proc/version")) {
               boost::iostreams::filtering_istream in;
               in.push(bohuno::iostreams::md5_filter());
               in.push(boost::iostreams::file_source("/proc/version"));
               std::string version;
               in >> version;
               id = in.component<0, bohuno::iostreams::md5_filter>()->hexdigest();
            }            
            
            return id;
         }

         void statistics(const asio::error_code& error)
         {
            if (!error) {

               std::string url = "https://update.bohuno.com/statistics.php?source=bohuno-pyzord";
               url += "&server=";
               url += get_computer_id();
               url += "&average-checks=";
               url += boost::lexical_cast<std::string>(check_statistics_.average());
               url += "&total-checks=";
               url += boost::lexical_cast<std::string>(check_statistics_.total());
               url += "&average-hits=";
               url += boost::lexical_cast<std::string>(hit_statistics_.average());
               url += "&total-hits=";
               url += boost::lexical_cast<std::string>(hit_statistics_.total());

               http::get(
                  io_service_,
                  http::url(url),
                  license_.username(), license_.password(),
                  boost::bind(&pyzord::handle_statistics_failure, this, _1),
                  boost::bind(&pyzord::handle_statistics_success, this, _1, _2, _3)
               );
            }
         }
         
         void schedule_statistics()
         {
            statistics_timer_.expires_from_now(boost::posix_time::seconds(5*60));
            statistics_timer_.async_wait(boost::bind(&pyzord::statistics, this, asio::placeholders::error));
         }

         //
         
         void start_update_download()
         {
            // Only start the next download if we are not shutting down and if there is an update to download

            if (!shutdown_ && !updates_to_download_.empty())
            {
               http::get(
                  io_service_,
                  http::url(updates_to_download_.front()),
                  license_.username(), license_.password(),
                  boost::bind(&pyzord::handle_update_download_failure, this, _1),
                  boost::bind(&pyzord::handle_update_download_success, this, _1, _2, _3)
               );
            }            
         }

         void handle_update_download_failure(const asio::error_code& error)
         {
            syslog_.error() << "Could not download update: " << error.message();

            // TODO Don't remove the download, instead, delay for a little while and try again.
            //updates_to_download_.pop_front();

            start_update_download();
         }

         void handle_update_download_success(unsigned int status, std::map<std::string,std::string> const& headers, std::string& data)
         {
            if (status == 401)
            {
               syslog_.error() << "The license was not accepted by the update server. Please contact <support@bohuno.com> for more information.";
               stop();
            }

            else if (status == 200)
            {
               updates_to_download_.pop_front();
               
               {
                  boost::iostreams::filtering_istream in;
                  in.push(boost::iostreams::gzip_decompressor());
                  in.push(boost::make_iterator_range(data));

                  try {
                     int n = database_.import(in);
                     syslog_.notice() << "Successfully downloaded update with " << n << " records.";                     
                  } catch (std::exception const& e) {
                     syslog_.error() << "Failed to import record: " << e.what();
                  }
               }

               start_update_download();
            } else {
               // TODO The update could not be found, skip it?
               syslog_.error() << "Unsuccessfully downloaded update. Update server returned status " << status;
               updates_to_download_.pop_front();
            }
         }
         
         void schedule_updates_scan(int time = 5 * 60)
         {
            updates_scan_timer_.expires_from_now(boost::posix_time::seconds(time));
            updates_scan_timer_.async_wait(boost::bind(&pyzord::updates_scan, this, asio::placeholders::error));
         }

         void handle_updates_scan_failure(const asio::error_code& error)
         {
            syslog_.error() << "Could not download list of available updates: " << error.message();
            this->schedule_updates_scan();
         }

         void handle_updates_scan_success(unsigned int status, std::map<std::string,std::string> const& headers, std::string& data)
         {
            if (status == 401)
            {
               syslog_.error() << "The license was not accepted by the update server."
                               << " Please contact <support@bohuno.com> for more information.";
               stop();
            }

            else if (status >= 200 && status <= 299)
            {
               bool was_empty = updates_to_download_.empty();

               // Get the highest update date

               boost::uint32_t highest_timestamp = 0;

               pyzor::hash hash; pyzor::record record;
               if (database_.lookup_last(hash, record)) {
                  highest_timestamp = record.updated();
               }

               boost::regex url_regex("<D:href>(https://[^/]+/pyzor/updates/(\\d{10})(\\d{10}))</D:href>",
                  boost::regex::perl);
   
               std::string::const_iterator i = data.begin();
               std::string::const_iterator e = data.end();
            
               boost::smatch matches;
               while (boost::regex_search(i, e, matches, url_regex))
               {
                  std::string url = matches[1].str();

                  // We are interested in the update if we have not seen it yet and if is around or after
                  // our last updated record.
                  
                  boost::uint32_t min = boost::lexical_cast<boost::uint32_t>(matches[2].str());
                  //boost::uint32_t max = boost::lexical_cast<boost::uint32_t>(matches[3].str());

                  if (min >= highest_timestamp) {
                     std::list<std::string>::iterator i = std::find(updates_to_download_.begin(),
                        updates_to_download_.end(), url);
                     if (i == updates_to_download_.end()) {
                        updates_to_download_.push_back(url);
                     }
                  }
                  
                  i = matches[0].second;
               }

               // Start a download if the queue was empty when we scanned for updates

               if (was_empty) {
                  start_update_download();
               }

               this->schedule_updates_scan();
            }
         }

         void updates_scan(const asio::error_code& error)
         {
            if (!error)
            {
               http::propget(
                  io_service_,
                  http::url("https://update.bohuno.com/pyzor/updates"),
                  license_.username(), license_.password(),
                  boost::bind(&pyzord::handle_updates_scan_failure, this, _1),
                  boost::bind(&pyzord::handle_updates_scan_success, this, _1, _2, _3)
               );
            }
         }

      private:

         void schedule_checkpoint()
         {
            checkpoint_timer_.expires_from_now(boost::posix_time::seconds(60));
            checkpoint_timer_.async_wait(boost::bind(&pyzord::checkpoint, this, asio::placeholders::error));
         }

         void checkpoint(const asio::error_code& error)
         {
            if (!error) {
               syslog_.debug() << "Running database checkpoint";
               database_.checkpoint();
               this->schedule_checkpoint();
            }
         }

      private:

         bool authorize_admin_request(pyzor::packet const& request, asio::ip::udp::endpoint const& sender_endpoint_)
         {
            return (sender_endpoint_.address() == asio::ip::address::from_string("127.0.0.1"));
         }

         void handle_receive_from(const asio::error_code& error, size_t bytes_recvd)
         {
            if (!error && bytes_recvd > 0)
            {
               // Parse the request
               
               pyzor::packet res, req;
         
               if (!pyzor::packet::parse(req, data_, bytes_recvd)) {
                  res.set("Thread", req.get("Thread"));
                  res.set("PV", "2.0");
                  res.set("Code", "400");
                  res.set("Diag", "Bad Request");
               } else {
                  res.set("Thread", req.get("Thread"));
                  res.set("PV", "2.0");
                  res.set("Diag", "OK");
                  res.set("Code", "200");
                  if (req.get("PV") != "2.0") {
                     res.set("Code", "505");
                     res.set("Diag", "Version Not Supported");                  
                  } else {
                     request_statistics_.report();
                     
                     if (req.get("Op") == "shutdown" || req.get("Op") == "statistics") {
                        if (!authorize_admin_request(req, sender_endpoint_)) {
                           res.set("Code", "401");
                           res.set("Diag", "Unauthorized");
                        } else {
                           if (req.get("Op") == "shutdown") {
                              shutdown_ = true;
                           } else if (req.get("Op") == "statistics") {
                              res.set("Stats-Average-Requests", boost::lexical_cast<std::string>(request_statistics_.average()));
                              res.set("Stats-Average-Checks", boost::lexical_cast<std::string>(check_statistics_.average()));
                              res.set("Stats-Average-Hits", boost::lexical_cast<std::string>(hit_statistics_.average()));
                              res.set("Stats-Total-Requests", boost::lexical_cast<std::string>(request_statistics_.total()));
                              res.set("Stats-Total-Checks", boost::lexical_cast<std::string>(check_statistics_.total()));
                              res.set("Stats-Total-Hits", boost::lexical_cast<std::string>(hit_statistics_.total()));
                           }
                        }
                     } else {
                        if (req.get("Op") == "check") {
                           check_statistics_.report();
                           if (verbose_) {
                              syslog_.debug() << "Request to check digest " << req.get("Op-Digest");
                           }                  
                           pyzor::record r;
                           if (database_.lookup(pyzor::hash(req.get("Op-Digest")), r) == true) {
                              hit_statistics_.report();
                           }
                           res.set("Count", boost::lexical_cast<std::string>(r.report_count()));
                           res.set("WL-Count", boost::lexical_cast<std::string>(r.whitelist_count()));
                        } else if (req.get("Op") == "ping") {
                           // Nothing to do for ping, just send back a plain response
                        } else {
                           res.set("Code", "501");
                           res.set("Diag", "Not supported operation");
                        }
                     }
                  }
               }
               
               // Send back the reply
               
               size_t length = res.archive(data_, sizeof(data_));
               
               socket_.async_send_to(
                  asio::buffer(data_, length),
                  sender_endpoint_,
                  boost::bind(&pyzord::handle_send_to, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
               );
            }
            
            // Receive the next incoming packet
            
            if (!shutdown_) {
               socket_.async_receive_from(
                  asio::buffer(data_, max_length),
                  sender_endpoint_,
                  boost::bind(&pyzord::handle_receive_from, this, asio::placeholders::error, asio::placeholders::bytes_transferred)
               );
            }
         }
   
         void handle_send_to(const asio::error_code& error, size_t bytes_sent)
         {
            // Nothing
         }
         
      private:
         
         pyzor::syslog& syslog_;
         asio::io_service& io_service_;

         boost::filesystem::path home_;
         std::string address_;
         std::string port_;
         bool verbose_;
         
         bohuno::license license_;
         bohuno::database database_;

         asio::deadline_timer statistics_timer_;         
         asio::deadline_timer checkpoint_timer_;
         asio::deadline_timer updates_scan_timer_;
         asio::ip::udp::socket socket_;
         asio::ip::udp::endpoint sender_endpoint_;
         enum { max_length = 8192 };
         char data_[max_length];
         bool shutdown_;
         std::list<std::string> updates_to_download_;
         bool download_in_progress_;
         
         pyzor::statistics_ring request_statistics_;
         pyzor::statistics_ring check_statistics_;
         pyzor::statistics_ring hit_statistics_;
   };

   // bohuno-pyzord [-v] [-x] [-d db-home] [-u user] [-a pyzor-addres] [-p pyzor-port]

   struct pyzord_options
   {
      public:
      
         pyzord_options()
            : verbose(false), debug(false), local("127.0.0.1"), port("24442"), home("/var/lib/bohuno-pyzord"),
              user("bohuno"), uid(0), gid(0)
         {
         }
      
      public:
      
         void usage()
         {
            std::cout << "usage: bohuno-pyzord [-v] [-x] [-d db-home] [-u user] [-a pyzor-addres] [-p pyzor-port]"
                      << std::endl;
         }
      
         bool parse(int argc, char** argv)
         {
            char c;
            while ((c = getopt(argc, argv, "hxvd:p:u:m:a:")) != EOF) {
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
                  case 'a':
                     local = optarg;
                     break;
                  case 'p':
                     port = optarg;
                     break;
                  case 'u': {
                     user = optarg;
                     break;
                  }
                  case 'h':
                  default:
                     usage();
                     return false;
               }
            }

            // Check if the user really exists

            struct passwd* passwd = getpwnam(user.c_str());
            if (passwd == NULL) {
               std::cout << "bohuno-pyzord: user '" << user << "' does not exist." << std::endl;
               return false;
            }
            uid = passwd->pw_uid;
            gid = passwd->pw_gid;
            
            return true;
         }
         
      public:
         
         bool verbose;
         bool debug;
         char* local;
         char* port;
         char* home;
         std::string user;
         uid_t uid;
         gid_t gid;
   };

   int pyzord_main(pyzord_options& options)
   {
      pyzor::syslog syslog("bohuno-pyzord", LOG_DAEMON, options.debug);

      syslog.notice() << "Starting bohuno-pyzord on pyzor://" << options.local << ":" << options.port
                      << " with database home " << options.home;

      try {
         asio::io_service io_service;
         bohuno::pyzord pyzord(syslog, io_service, options.home, options.local, options.port, options.verbose);
         
         // Block all signals for background thread.
         sigset_t new_mask;
         sigfillset(&new_mask);
         sigset_t old_mask;
         pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);
         
         // Run server in background thread.
         asio::thread t(boost::bind(&bohuno::pyzord::run, &pyzord));
         
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
         pyzord.stop();
         t.join();
      } catch (std::exception& e) {
         syslog.error() << "Server exited with failure: " << e.what();
      }
      
      syslog.notice() << "Server exited gracefully.";
      
      return 128;
   }

}

int main(int argc, char** argv)
{
   bohuno::pyzord_options options;
   if (options.parse(argc, argv)) {
      if (options.debug) {
         if (setgid(options.gid) != 0 || setuid(options.uid) != 0) {
            std::cerr << "Cannot change to user " << options.user << std::endl;
            return 1;
         } else {
            std::cout << getuid() << " " << getgid() << " " << geteuid() << " " << getegid() << std::endl;
            return bohuno::pyzord_main(options);
         }
      } else {
         return pyzor::daemonize(argc, argv, boost::bind(bohuno::pyzord_main, options),
            options.uid, options.gid, "/var/run/bohuno-pyzord.pid");
      }
   } else {
      return 1;
   }
}
