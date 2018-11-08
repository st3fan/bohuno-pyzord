#!/usr/bin/python

if __name__ == "__main__":

    import sys
    sys.path.append('../../python')

    from deb import Deb
    
    deb = Deb("bohuno-pyzord", "1.0", "1", "i386", "Bohuno <info@bohuno.com>")
    
    deb.add_dependency("libc6", ">=", "2.6-1")
    deb.add_dependency("libdb4.6", ">=", "4.6.21")
    deb.add_dependency("libboost-iostreams1.34.1", ">=", "1.34.1")
    deb.add_dependency("libboost-regex1.34.1", ">=", "1.34.1")
    deb.add_dependency("libboost-filesystem1.34.1", ">=", "1.34.1")
    deb.add_dependency("libboost-signals1.34.1", ">=", "1.34.1")

    deb.add_executable("../../sources/bohuno-pyzord", "./usr/sbin/bohuno-pyzord")
    deb.add_executable("../../sources/bohuno-pyzord-setup", "./usr/sbin/bohuno-pyzord-setup")
    deb.add_executable("bohuno-pyzord.initd", "./etc/init.d/bohuno-pyzord")

    deb.set_prerm("bohuno-pyzord.prerm")
    deb.set_postrm("bohuno-pyzord.postrm")
    deb.set_postinst("bohuno-pyzord.postinst")
    
    print "Created package %s" % deb.write("./")

