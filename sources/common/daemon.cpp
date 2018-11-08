
#include <iostream>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "daemon.hpp"

namespace pyzor {

   static int wrapper(int argc, char** argv, daemon_function_t f, uid_t uid, gid_t gid,
      const char* pid_file)
   {
      while (true) {
         pid_t p = fork();         
         if (p == -1) {
            return EXIT_FAILURE;
         } else if (p == 0) {
            // Change user
            if (uid != 0 && gid != 0) {
               setgid(gid);
               setuid(uid);
            }
            return f(argc, argv);
         } else {

            // Write the pid file
            std::cout << "Writing to pid file " << pid_file << std::endl;            
            FILE* fp = fopen(pid_file, "w");
            if (fp != NULL) {
               fprintf(fp, "%d\n", p);
               fclose(fp);
            }

            // Wait for the daemon to finish
            int status;
            pid_t pid = waitpid(-1, &status, 0);

            // Remove the pid file
            std::cout << "Deleting pid file " << pid_file << std::endl;
            unlink(pid_file);
            
            if (pid == -1) {
               return EXIT_FAILURE;
            }
            // Exit code 128 means that the daemon exited
            if ((status >> 8) == 128) {
               break;
            }
            // Otherwise, sleep and restart
            ::sleep(5);
         }
      }
      return EXIT_SUCCESS;
   }

   int daemonize(int argc, char** argv, daemon_function_t f, uid_t uid, gid_t gid,
      const char* pid_file)
   {
      int result = EXIT_SUCCESS;

      pid_t pid = fork();
      if (pid == -1) {
         result = EXIT_FAILURE;
      } else if (pid == 0) {
         // Become session leader
         setsid();
         // Change to the root filesystem so that we won't accidentally prevent
         // filesystems from being unmounted
         chdir("/");
         // Close all open file descriptors
         for (int i = 0; i < 1024; i++) {
            (void) close(i);
         }
         // Attach file desciptors to /dev/null
         (void) open("/dev/null", O_RDWR);
         (void) dup(0);
         (void) dup(0);
         // Client process, will spawn the wrapper
         result = wrapper(argc, argv, f, uid, gid, pid_file);
      }
      
      return result;
   }

}

#ifdef DAEMON_TEST
int my_daemon(int argc, char** argv)
{
   int i = 0;
   while (true) {
      std::cout << "Hello, this is the daemon" << std::endl;
      sleep(5);
      if (i++ == 3) {
         return 128;
      }
   }
   return 0;
}

int main(int argc, char** argv)
{
   return pyzor::daemonize(argc, argv, my_daemon);
}
#endif
