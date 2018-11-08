#!/usr/bin/env python

import sys, struct, encodings.hex_codec

f = open(sys.argv[1])

while True:
    data = f.read(52)
    if data == None:
        break
    record = struct.unpack('!20sLLLLLLLL', data)
    print record[0].encode("hex")
