// arcfour.hpp

#ifndef ARCFOUR_HPP
#define ARCFOUR_HPP

#include <memory>
#include <boost/iostreams/filter/symmetric.hpp>

namespace bohuno {

   namespace iostreams {

      namespace detail {

         ///

         template< typename Alloc = std::allocator<char> >
         class arcfour_filter_impl
         {
            public:

               typedef char char_type;

            public:

               arcfour_filter_impl(std::string const& key);

               bool filter(const char*& src_begin, const char* src_end, char*& dest_begin, char* dest_end, bool flush);
               void close();

            private:

               char encrypt(char c);
            
            private:
               
               std::string key_;
               char state_[256];
               int x_;
               int y_;               
         };
         
      }

      ///

      template< typename Alloc = std::allocator<char> >
      struct basic_arcfour_filter : boost::iostreams::symmetric_filter<detail::arcfour_filter_impl<Alloc>, Alloc>
      {
         private:
            
            typedef detail::arcfour_filter_impl<Alloc>                   impl_type;
            typedef boost::iostreams::symmetric_filter<impl_type, Alloc>  base_type;
            
         public:
            
            typedef typename base_type::char_type                         char_type;
            typedef typename base_type::category                          category;
            
         public:
            
            basic_arcfour_filter(std::string const& key, int buffer_size = boost::iostreams::default_device_buffer_size);
      };
      
      typedef basic_arcfour_filter<> arcfour_filter;


      // Implementation

      namespace detail {

         // Implemented according to http://en.wikipedia.org/wiki/RC4_(cipher)

         template<typename Alloc>
         arcfour_filter_impl<Alloc>::arcfour_filter_impl(std::string const& key)
            : key_(key)
         {
            for (int i = 0; i <= 255; i++) {
               state_[i] = i;
            }

            x_ = y_ = 0;

            for (int i = 0; i <= 255; i++) {
               x_ = (key_[i % key.size()] + state_[i] + x_) & 0xff;
               std::swap(state_[i], state_[x_]);
            }
            
            x_ = 0;
         }
         
         template<typename Alloc>
         bool arcfour_filter_impl<Alloc>::filter(const char*& src_begin, const char* src_end, char*& dest_begin, char* dest_end, bool flush)
         {
            while (src_begin != src_end && dest_begin != dest_end) {
               *dest_begin++ = encrypt(*src_begin++);
            }

            return false;
         }

         template<typename Alloc>
         char arcfour_filter_impl<Alloc>::encrypt(char c)
         {
            x_ = (x_ + 1) & 0xff;
            y_ = (state_[x_] + y_) & 0xff;
            std::swap(state_[x_], state_[y_]);

            return c ^ (state_[(state_[x_] + state_[y_]) & 0xff]);
         }

         template<typename Alloc>
         void arcfour_filter_impl<Alloc>::close()
         {
         }

      } // namespace detail
   
      template<typename Alloc>
      basic_arcfour_filter<Alloc>::basic_arcfour_filter(std::string const& key, int buffer_size)
         : base_type(buffer_size, key)
      {
      }
      
   }

}

#endif
