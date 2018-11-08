// url.cpp

#include <stdexcept>
#include <boost/lexical_cast.hpp>

#include "url.hpp"

namespace http {

   url::url(std::string const& s)
   {
      parse(s);
   }

   std::string const& url::scheme() const
   {
      return scheme_;
   }

   void url::scheme(std::string const& scheme)
   {
      scheme_ = scheme;
      update();
   }

   std::string const& url::host() const
   {
      return host_;
   }

   void url::host(std::string const& host)
   {
      host_ = host;
      update();
   }
	
   boost::uint16_t url::port() const
   {
      return port_;
   }

   void url::port(boost::uint16_t port)
   {
      port_ = port;
      update();
   }
			
   std::string const& url::path() const
   {
      return path_;
   }

   void url::path(std::string const& path)
   {
      path_ = path;
      update();
   }

   std::string url::str() const
   {
      return url_;
   }

   void url::update()
   {
      url_.clear();
      if (!path_.empty()) {
         url_ = scheme_ + std::string("://") + host_ + std::string(":") + boost::lexical_cast<std::string>(port_) + path_;
      } else {
         url_ = scheme_ + std::string("://") + host_ + std::string(":") + boost::lexical_cast<std::string>(port_);
      }      
   }

   void url::parse(std::string const& s)
   {
      size_t n = 0;
		
      // Scheme
		
      while (n < s.length() && s[n] != ':') {
         scheme_.push_back(s[n++]);
      }
		
      // Seperator
		
      if (s[n] != ':' || s[n+1] != '/' || s[n+2] != '/') {
         throw std::runtime_error("Invalid URL");
      }
		
      n += 3;
		
      // Hostname
      
      while (n < s.length() && s[n] != ':' && s[n] != '/') {
         host_.push_back(s[n++]);
      }
		
      // Port
		
      if (s[n] == ':') {
         std::string tmp;
         n++;
         while (n < s.length() && std::isdigit(s[n]) != 0) {
            tmp.push_back(s[n++]);
            port_ = std::atoi(tmp.c_str());
            if (port_ == 0) {
               throw std::runtime_error("Invalid URL");
            }
         }
      } else {
         // Much nicer would be a 'UrlSchemeRegistry' that contains info about the schemes
         if (scheme_ == "http") {
            port_ = 80;
         } else if (scheme_ == "https") {
            port_ = 443;
         } else if (scheme_ == "mysql") {
            port_ = 3306;
         } else if (scheme_ == "ftp") {
            port_ = 21;
         } else {
            throw std::runtime_error("Invalid URL: Don't know default port number for scheme");
         }
      }

      // Path
		
      if (n < s.length()) {
         if (s[n] != '/') {
            throw std::runtime_error("Invalid URL");
         }
         path_ = s.substr(n);
      }
      
      url_ = s;      
   }

   std::ostream& operator<<(std::ostream& stream, url const& u)
   {
      stream << u.str();
      return stream;
   }

}
