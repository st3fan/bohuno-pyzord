#!/usr/bin/env python

import hashlib, random, socket, struct, sys, time, encodings.hex_codec


def generateRandomSignature():
    m = hashlib.sha1()
    m.update(str(time.time()))
    m.update(str(random.random()))
    return m.digest()

def generateUpdate(type, signature, time):
    types = { 'erase': 0, 'report': 1, 'whitelist': 2 }
    return struct.pack('20sLL', signature, types[type], time)

def sendUpdate(type, signature):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", 5555))
    s.send(generateUpdate(type, signature, int(time.time())))
    s.close()

def parseSignature(s):
    return s.decode('hex')

if sys.argv[2] == 'random':
    sendUpdate(sys.argv[1], generateRandomSignature())
else:
    sendUpdate(sys.argv[1], parseSignature(sys.argv[2]))
