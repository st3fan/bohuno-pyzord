// syslog.hpp

#ifndef SYSLOG_HPP
#define SYSLOG_HPP

#include <string>
#include <boost/cstdint.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

#include <syslog.h>
#include <stdarg.h>

namespace pyzor {

   class syslog : boost::noncopyable
   {
      public:

         class logger
         {
            public:
         
               logger(int priority, bool console);
               ~logger();

            public:
               
               logger& operator << (const std::string &s);
               logger& operator << (int v);
               logger& operator << (unsigned int v);
               logger& operator << (boost::uint64_t v);
               logger& operator << (double v);
               
            private:

               int priority_;
               bool console_;
               std::string message_;
         };

      public:

         syslog(std::string const& name, int facility = LOG_DAEMON, bool console = false);
         ~syslog();

      public:

         logger info();
         logger alert();
         logger error();
         logger critical();
         logger notice();
         logger debug();
         logger warning();

      private:
         
         std::string name_;
         int facility_;
         bool console_;
   };

   typedef boost::shared_ptr<syslog> syslog_ptr;

}

#endif
