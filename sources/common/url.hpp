// url.hpp

#ifndef URL_HPP
#define URL_HPP

#include <iostream>
#include <string>
#include <boost/cstdint.hpp>

namespace http {

   class url
   {
      public:

         url(std::string const& s);

      public:

         std::string const& scheme() const;
         void scheme(std::string const& scheme);

         std::string const& host() const;
         void host(std::string const& host);
	
         boost::uint16_t port() const;
         void port(boost::uint16_t port);
			
         std::string const& path() const;
         void path(std::string const& path);

         std::string str() const;
		
      private:
         
         void update();
         void parse(std::string const& s);
	
      private:

         std::string url_;
         std::string scheme_;
         boost::uint16_t port_;
         std::string host_;
         std::string path_;
   };

   std::ostream& operator<<(std::ostream& stream, url const& t);

}

#endif // URL_HPP
