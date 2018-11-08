#!/usr/bin/env python

import hashlib, random, sys, struct, time, socket

def outputHeader(version):
    sys.stdout.write(struct.pack('!I', version))

def outputRecordV1(signature, r_count, r_entered, r_updated, wl_count, wl_entered, wl_updated):
    sys.stdout.write(struct.pack('!20sLLLLLL', signature, r_count, r_entered, r_updated,
        wl_count, wl_entered, wl_updated))

def outputRecordV2(signature, entered, updated, r_count, r_entered, r_updated, wl_count, wl_entered, wl_updated):
    sys.stdout.write(struct.pack('!20sLLLLLLLL', signature, entered, updated, r_count, r_entered, r_updated,
        wl_count, wl_entered, wl_updated))

def generateRandomSignature():
    m = hashlib.sha1()
    m.update(str(time.time()))
    m.update(str(random.random()))
    return m.digest()

#outputHeader(2)

#now = int(time.time())
#for t in range(now-86400, now):
#    d = random.randint(0,3600)
#    outputRecordV2(generateRandomSignature(), t, t+d, random.randint(1,5), t, t+d, 0, 0, 0)

now = int(time.time())
for d in range(0,128000):
    t = now - (86400 * random.randint(0, 3*28))
    outputRecordV2(generateRandomSignature(), t, t, random.randint(1,5), t, t, 0, 0, 0)
