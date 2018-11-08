// md5_filter.hpp

#ifndef MD5_FILTER
#define MD5_FILTER

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/char_traits.hpp>
#include <boost/iostreams/operations.hpp>
#include <boost/iostreams/pipeline.hpp>

#include <openssl/md5.h>

namespace bohuno {

   namespace iostreams {

      using namespace boost::iostreams;
      
      template<typename Ch>
      class basic_md5_filter
      {
         public:
            
            typedef Ch char_type;
            
            struct category : dual_use, filter_tag, multichar_tag, optimally_buffered_tag { };

            explicit basic_md5_filter()
            {
               MD5_Init(&context_);
            }

            std::streamsize optimal_buffer_size() const
            {
               return 0;
            }
            
            template<typename Source>
            std::streamsize read(Source& src, char_type* s, std::streamsize n)
            {
               std::streamsize result = iostreams::read(src, s, n);
               if (result == -1) {
                  return -1;
               } else {
                  MD5_Update(&context_, s, result);               
                  return result;
               }
            }

            template<typename Sink>
            std::streamsize write(Sink& snk, const char_type* s, std::streamsize n)
            {
               std::streamsize result = iostreams::write(snk, s, n);
               MD5_Update(&context_, s, result);
               return result;
            }

         public:

            std::string digest()
            {
               unsigned char data[MD5_DIGEST_LENGTH];
               MD5_Final(data, &context_);
               return std::string((char*) data, sizeof(data));
            }

            std::string hexdigest()
            {
               static char digits[] = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };

               unsigned char data[MD5_DIGEST_LENGTH];
               MD5_Final(data, &context_);
            
               std::string hex;
               
               for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                  hex.push_back(digits[ (data[i] & 0xf0) >> 4 ]);
                  hex.push_back(digits[ (data[i] & 0x0f) >> 0 ]);
               }

               return hex;
            }
            
         private:
            
            MD5_CTX context_;
      };
      
      BOOST_IOSTREAMS_PIPABLE(basic_md5_filter, 1)

      typedef basic_md5_filter<char>     md5_filter;
      typedef basic_md5_filter<wchar_t>  wmd5_filter;

   }

}

#endif // MD5_FILTER
