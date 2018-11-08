#!/bin/sh

### BEGIN INIT INFO
# Provides:          pyzord-master
# Required-Start:    networking
# Required-Stop:     networking
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start the pyzord master.
### END INIT INFO


PATH=/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/local/sbin/pyzord-master
NAME=pyzord-master
DESC="pyzord master"
PIDFILE=/var/run/$NAME.pid
SCRIPTNAME=/etc/init.d/$NAME

DAEMON_OPTS="-u pyzor -l 82.94.255.106"

test -x $DAEMON || exit 0

set -e

. /lib/lsb/init-functions

export LD_LIBRARY_PATH=/usr/local/lib

case "$1" in
  start)
  log_daemon_msg "Starting $DESC"
    if ! $DAEMON $DAEMON_OPTS ; then
        log_end_msg 1
        exit 1
    else
        log_end_msg 0
    fi
    ;;
  stop)
  log_daemon_msg "Stopping $DESC"
  if /usr/local/sbin/pyzord-shutdown; then
          log_end_msg 0
          else
          log_end_msg 1
        exit 1
    fi
    ;;
  *)
  echo "Usage: $SCRIPTNAME {start|stop|restart|reload|force-reload}" >&2
  exit 1
  ;;
esac

exit 0
