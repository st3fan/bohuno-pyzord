
#include <ctype.h>

#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>

#include "packet.hpp"

#include <iostream>

namespace pyzor {

   bool packet::parse(packet& packet, const char* buffer, size_t length)
   {
      std::string tmp(buffer, length);

      boost::char_separator<char> separator("\r\n");
      boost::tokenizer< boost::char_separator<char> > tokenizer(tmp, separator);
      
      for (boost::tokenizer< boost::char_separator<char> >::iterator i = tokenizer.begin(); i!=tokenizer.end(); ++i) {
         boost::regex regex("^(\\S+?):\\s+(.*)$", boost::regex::perl);
         boost::smatch matches;
         if (boost::regex_match(*i, matches, regex)) {
            packet.attributes_[matches[1].str()] = matches[2].str();
         }
      }
      
      // Packets must have PV, Op, Time and Thread

      if (!packet.has("PV") || !packet.has("Op") || !packet.has("Time") || !packet.has("Thread")) {
         return false;
      }
      
      // Check if the check, report and whitelist commands have a correct digest
      
      if (packet.get("Op") == "check" || packet.get("Op") == "report" || packet.get("Op") == "whitelist") {
         if (!packet.has("Op-Digest")) {
            return false;
         } else {
            std::string digest = packet.get("Op-Digest");
            if (digest.length() != 40) {
               return false;
            }
            for (size_t i = 0; i < digest.length(); i++) {
               if (isxdigit(digest[i]) == 0) {
                  return false;
               }
            }
         }
      }

      return true;
   }

   packet::packet()
   {
   }

   packet::packet(const char* buffer, size_t length)
   {
      std::string tmp(buffer, length);            

      boost::char_separator<char> separator("\r\n");
      boost::tokenizer< boost::char_separator<char> > tokenizer(tmp, separator);
      
      for (boost::tokenizer< boost::char_separator<char> >::iterator i = tokenizer.begin(); i!=tokenizer.end(); ++i) {
         boost::regex regex("^(\\w+):\\s+(.*)$", boost::regex::perl);
         boost::smatch matches;
         if (boost::regex_match(*i, matches, regex)) {
            attributes_[matches[1].str()] = matches[2].str();
         }
      }
   }

   size_t packet::archive(char* buffer, size_t buffer_size) const
   {
      std::string tmp;
      
      for (std::map<std::string,std::string>::const_iterator i = attributes_.begin(); i != attributes_.end(); ++i) {
         tmp += i->first;
         tmp += ": ";
         tmp += i->second;
         tmp += "\n";
      }
      
      std::strncpy(buffer, tmp.c_str(), buffer_size);
      return tmp.length();
   }

   bool packet::has(std::string const& name)
   {
      return attributes_.find(name) != attributes_.end();
   }
   
   const std::string& packet::get(std::string const& name)
   {
      return attributes_[name];
   }

   void packet::set(std::string const& name, std::string const& value)
   {
      attributes_[name] = value;
   }

   void packet::remove(std::string const& name)
   {
      std::map<std::string,std::string>::iterator i = attributes_.find(name);
      if (i != attributes_.end()) {
         attributes_.erase(i);
      }
   }
   
}
