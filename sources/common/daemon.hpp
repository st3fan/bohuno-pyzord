
#ifndef DAEMON_HPP
#define DAEMON_HPP

#include <string>
#include <vector>

#include <boost/function.hpp>

namespace pyzor {

   typedef boost::function<int (int argc, char** argv)> daemon_function_t;
   int daemonize(int argc, char** argv, daemon_function_t f, uid_t uid = 0, gid_t gid = 0,
      const char* pid_file = 0L);

}

#endif
