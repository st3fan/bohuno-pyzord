#!/usr/bin/env python

import base64, md5, random, sys, time

# This comes from http://en.wikipedia.org/wiki/Rc4

class WikipediaARC4:
    def __init__(self, key = None):
        self.state = range(256) # Initialize state array with values 0 .. 255
        self.x = self.y = 0 # Our indexes. x, y instead of i, j 
        if key is not None:
            self.init(key)
        
    # KSA
    def init(self, key):
        for i in range(256):
            self.x = (ord(key[i % len(key)]) + self.state[i] + self.x) & 0xFF
            self.state[i], self.state[self.x] = self.state[self.x], self.state[i]
        self.x = 0
            
    # PRGA
    def crypt(self, input):
        output = [None]*len(input)
        for i in xrange(len(input)):
            self.x = (self.x + 1) & 0xFF
            self.y = (self.state[self.x] + self.y) & 0xFF
            self.state[self.x], self.state[self.y] = self.state[self.y], self.state[self.x]
            r = self.state[(self.state[self.x] + self.state[self.y]) & 0xFF]
            output[i] = chr(ord(input[i]) ^ r)
        return ''.join(output)

if __name__ == "__main__":

    if len(sys.argv) != 3:
        print "usage: generate-license <real-name> <username>"
        sys.exit(1)

    KEY = "Work it, make it, do it, makes us harder, better, faster, STRONGER!"
    
    password = md5.new(str(time.time()) + str(random.random()) + str(random.random())).hexdigest()
    license = base64.standard_b64encode(WikipediaARC4(KEY).crypt('\t'.join([sys.argv[1], sys.argv[2], password])))
    
    print
    print "License key for %s" % sys.argv[1]
    print ""
    print " %s" % license
    print ""
    print "Add the following user to the update server:"
    print
    print " Username  : %s" % sys.argv[2]
    print " Password  : %s" % password
    print

