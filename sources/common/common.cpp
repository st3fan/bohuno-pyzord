// common.cpp

#include <arpa/inet.h>
#include <signal.h>

#include <string>
#include <asio.hpp>

#include "common.hpp"
#include "record.hpp"

namespace pyzor {

   u_int32_t pyzor_hash_function(DB* dbp, const void* key, u_int32_t len)
   {
      const u_int8_t *k, *e;
      u_int32_t h;
      
      k = (const u_int8_t*) key;
      e = k + len;
      for (h = 0; k < e; ++k) {
         h *= 16777619;
         h ^= *k;
      }
      
      return h;
   }

   bool decode_signature(std::string const& hex, unsigned char* data)
   {
      if (hex.length() != 40) {
         return false;
      }
      
      for (int i = 0; i < 20; i++) {
         char s[3];
         s[0] = hex[(i*2)];
         s[1] = hex[(i*2)+1];
         s[2] = 0x00;
         data[i] = strtol(s, NULL, 16);
      }
      
      return true;
   }
   
   int create_time_key(DB* db, const DBT* pkey, const DBT* pdata, DBT* skey)
   {
      pyzor::record* r = (pyzor::record*) pdata->data;
      
      memset(skey, 0, sizeof(DBT));
      skey->data = &(r->updated_);
      skey->size = sizeof(time_t);
      
      return 0;
   }

   int compare_time_key(DB *dbp, const DBT *a, const DBT *b)
   {
      time_t at, bt;

      // TODO This can be optimized since time_t is a long on both OS X and Linux.
      // TODO The data is also aligned properly so the copy is not required.
      
      memcpy(&at, a->data, sizeof(time_t));
      memcpy(&bt, b->data, sizeof(time_t));

      at = ntohl(at);
      bt = ntohl(bt);
      
      if (at < bt) {
         return -1;
      } else if (at > bt) {
         return 1;
      } else {
         return 0;
      }
   }

   void run_in_thread(const boost::function0<void>& run, const boost::function0<void>& stop)
   {
      // Block all signals for background thread.
      sigset_t new_mask;
      sigfillset(&new_mask);
      sigset_t old_mask;
      pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);

      // Run server in background thread.
      asio::thread t(run);

      // Restore previous signals.
      pthread_sigmask(SIG_SETMASK, &old_mask, 0);

      // Wait for signal indicating time to shut down.
      sigset_t wait_mask;
      sigemptyset(&wait_mask);
      sigaddset(&wait_mask, SIGINT);
      sigaddset(&wait_mask, SIGQUIT);
      sigaddset(&wait_mask, SIGTERM);
      pthread_sigmask(SIG_BLOCK, &wait_mask, 0);
      int sig = 0;
      sigwait(&wait_mask, &sig);

      // Stop the server.
      stop();
      
      t.join();
   }
   
}
