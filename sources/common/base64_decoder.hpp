// base64_decoder.hpp

#ifndef BASE64_DECODER
#define BASE64_DECODER

#include <stdexcept>

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/operations.hpp>

namespace bohuno {

   namespace iostreams {

      static const unsigned char base64_decoder_table[256] = {
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255,  62, 255, 255, 255,  63,
          52,  53,  54,  55,  56,  57,  58,  59,  60,  61, 255, 255,
         255, 254, 255, 255, 255,   0,   1,   2,   3,   4,   5,   6,
           7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,
          19,  20,  21,  22,  23,  24,  25, 255, 255, 255, 255, 255,
         255,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,
          37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
          49,  50,  51, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
         255, 255, 255, 255
      };

      class base64_decoder : public boost::iostreams::multichar_input_filter
      {
         public:
            
            base64_decoder()
            {
               reset();
            }
            
         public:

            template<typename Source>
            std::streamsize read(Source& source, char* out, std::streamsize n)
            {
               if (n < 3) {
                  throw std::runtime_error("buffer too small, need at least 3 bytes to decode base64");
               }

               char buffer[(n / 3) * 4]; // Base64 works with 3 byte chunks that are encoded as 4 bytes.

               std::streamsize result = boost::iostreams::read(source, buffer, (n / 3) * 4);
               if (result == -1) {
                  return -1;
               }

               char* p = out;

               for (std::streamsize i = 0; i < result; i++)
               {
                  buffer_[buffer_index_++] = buffer[i];
                  if (buffer_index_ == 4)
                  {
                     boost::uint32_t v = 0;
                  
                     v |= (boost::uint32_t) (base64_decoder_table[(int) buffer_[0]] << (3*6));
                     v |= (boost::uint32_t) (base64_decoder_table[(int) buffer_[1]] << (2*6));
                     v |= (boost::uint32_t) (base64_decoder_table[(int) buffer_[2]] << (1*6));
                     v |= (boost::uint32_t) (base64_decoder_table[(int) buffer_[3]] << (0*6));

                     char c1 = (v >> 16) & 0xff;
                     char c2 = (v >> 8) & 0xff;
                     char c3 = (v >> 0) & 0xff;

                     int n = buffer_[3] == '=' ? (buffer_[2] == '=' ? 1 : 2) : 3;
                     
                     if (n >= 1) *p++ = c1;
                     if (n >= 2) *p++ = c2;
                     if (n >= 3) *p++ = c3;
                     
                     reset();
                  }
               }
                  
               return (p - out);
            }

         private:

            void reset()
            {
               buffer_index_ = 0;
               for (int i = 0; i < 4; i++) {
                  buffer_[i] = 0x00;
               }
            }

         private:

            char buffer_[4];
            int buffer_index_;
      };

      BOOST_IOSTREAMS_PIPABLE(base64_decoder, 0)

   }

}

#endif // BASE64_DECODER
