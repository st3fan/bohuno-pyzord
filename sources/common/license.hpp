
#ifndef LICENSE_HPP
#define LICENSE_HPP

#include <string>
#include <boost/filesystem/path.hpp>

namespace bohuno {

   struct license {
         
      public:

         explicit license(std::string const& license);
         license(boost::filesystem::path const& path);

      public:

         std::string const& realname();
         std::string const& username();
         std::string const& password();

      private:

         std::string decode(std::string const& license);
         void parse(std::string const& license);
         
      private:
         
         std::string realname_;
         std::string username_;
         std::string password_;
         
   };

}

#endif
