
#ifndef PACKET_HPP
#define PACKET_HPP

#include <map>
#include <string>

namespace pyzor {
   
   class packet
   {
      public:

         static bool parse(packet& packet, const char* buffer, size_t length);

      public:
         
         packet();
         packet(const char* buffer, size_t length);

      public:
         
         size_t archive(char* buffer, size_t buffer_size) const;

      public:

         bool has(std::string const& name);
         const std::string& get(std::string const& name);
         void set(std::string const& name, std::string const& value);
         void remove(std::string const& name);

      private:

         std::map<std::string,std::string> attributes_;
   };   

}

#endif
