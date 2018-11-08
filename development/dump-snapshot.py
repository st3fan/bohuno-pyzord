#!/usr/bin/env python

import gzip, sys, struct, encodings.hex_codec

f = gzip.open(sys.argv[1])

version = struct.unpack('!L', f.read(4))[0]
if version != 2:
    print "Snapshot is not version 2"
    sys.exit(1)

while True:
    data = f.read(52)
    if data == None:
        break
    record = struct.unpack('!20sLLLLLLLL', data)
    print "%s %8d %8d %8d" % (record[0].encode("hex"), record[1], record[2], record[3])
