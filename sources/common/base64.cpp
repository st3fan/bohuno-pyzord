// base64.cpp

#include "base64.hpp"

namespace base64 {
	
   namespace {
		
      const char* gBase64EncodeTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

      static const unsigned char gBase64DecodeTable[256] = {
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
      
   }
	
   std::string encode(std::string const& string)
   {
      std::string encoded;
      
      size_t i;
      for (i = 0; i < string.length() - 2; i += 3)
      {
         encoded += gBase64EncodeTable[(string[i] >> 2) & 0x3F];
         encoded += gBase64EncodeTable[((string[i] & 0x3) << 4) | ((int) (string[i + 1] & 0xF0) >> 4)];
         encoded += gBase64EncodeTable[((string[i + 1] & 0xF) << 2) | ((int) (string[i + 2] & 0xC0) >> 6)];
         encoded += gBase64EncodeTable[string[i + 2] & 0x3F];
      }
      
      if (i < string.length())
      {
         encoded += gBase64EncodeTable[(string[i] >> 2) & 0x3F];
         
         if (i == (string.length() - 1)) {
            encoded += gBase64EncodeTable[((string[i] & 0x3) << 4)];
            encoded += '=';
         } else {
            encoded += gBase64EncodeTable[((string[i] & 0x3) << 4) | ((int) (string[i + 1] & 0xF0) >> 4)];
            encoded += gBase64EncodeTable[((string[i + 1] & 0xF) << 2)];
         }
         
         encoded += '=';
      }
      
      return encoded;
   }
   
   std::string decode(std::string const& data)
   {
      std::string decoded;
      
      unsigned long t, x, y;
      unsigned char c;
      int           g;
      
      g = 3;
      for (x = y = t = 0; x < data.length(); x++)
      {
         c = gBase64DecodeTable[data[x]&0xFF];
         if (c == 255) continue;
         if (c == 254) { c = 0; g--; }
         t = (t<<6)|c;
         if (++y == 4) {
            decoded += (unsigned char)((t>>16)&255);
            if (g > 1) decoded += (unsigned char)((t>>8)&255);
            if (g > 2) decoded += (unsigned char)(t&255);
            y = t = 0;
         }
      }
      
      return decoded;
   }
   
}
