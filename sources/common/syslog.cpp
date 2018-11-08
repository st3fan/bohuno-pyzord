// syslog.cpp

#include "syslog.hpp"

#include <iostream>

namespace pyzor {

   syslog::logger::logger(int priority, bool console)
      : priority_(priority), console_(console)
   {
   }
   
   syslog::logger::~logger()
   {
      if (console_) {
         std::cout << message_ << std::endl;
      }
      ::syslog(priority_, message_.c_str());
   }
   
   syslog::logger& syslog::logger::operator<<(const std::string &s)
   {
      message_.append(s);
      return *this;
   }
   
   syslog::logger& syslog::logger::operator<<(int v)
   {
      message_.append(boost::lexical_cast<std::string>(v));
      return *this;
   }
   
   syslog::logger& syslog::logger::operator<<(unsigned int v)
   {
      message_.append(boost::lexical_cast<std::string>(v));
      return *this;
   }
   
   syslog::logger& syslog::logger::operator<<(boost::uint64_t v)
   {
      message_.append(boost::lexical_cast<std::string>(v));
      return *this;
   }

   syslog::logger& syslog::logger::operator<<(double v)
   {
      message_.append(boost::lexical_cast<std::string>(v));
      return *this;
   }
               
   syslog::syslog(std::string const& name, int facility, bool console)
      : name_(name), facility_(facility), console_(console)
   {
      int flags = LOG_PID;
      if (console_) {
         flags |= LOG_CONS;
      }
      
      ::openlog(name.c_str(), flags, facility);
   }
   
   syslog::~syslog()
   {
      ::closelog();
   }

   syslog::logger syslog::info()
   {
      return logger(LOG_INFO, console_);
   }

   syslog::logger syslog::alert()
   {
      return logger(LOG_ALERT, console_);
   }

   syslog::logger syslog::error()
   {
      return logger(LOG_ERR, console_);
   }

   syslog::logger syslog::critical()
   {
      return logger(LOG_CRIT, console_);
   }
   
   syslog::logger syslog::notice()
   {
      return logger(LOG_NOTICE, console_);
   }
   
   syslog::logger syslog::debug()
   {
      return logger(LOG_DEBUG, console_);
   }

   syslog::logger syslog::warning()
   {
      return logger(LOG_WARNING, console_);
   }
   
}
