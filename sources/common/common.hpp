// common.hpp

#ifndef COMMON_HPP
#define COMMON_HPP

#include <db.h>

#include <string>
#include <boost/cstdint.hpp>
#include <boost/function.hpp>

namespace pyzor {

   //typedef boost::uint8_t hash_t[20];

   u_int32_t pyzor_hash_function(DB* dbp, const void* key, u_int32_t len);
   bool decode_signature(std::string const& hex, unsigned char* data);

   int create_time_key(DB* db, const DBT* pkey, const DBT* pdata, DBT* skey);
   int compare_time_key(DB *dbp, const DBT *a, const DBT *b);

   void run_in_thread(const boost::function0<void>& start, const boost::function0<void>& stop);
}

#endif
