#!/usr/bin/env python

import os
import subprocess
import sys


def replay(filename):
    infos = [eval(line, {}) for line in open(filename)]
    infos = [info for info in infos
             if not info["recmake"]]
    for index, info in enumerate(infos):
        name = info["file"]
        topdir = "/work/nacl/2/googleclient/native_client/glibc-2.9/build/"
        if name.startswith(topdir):
            name = name[len(topdir):]
        print "[%i/%i] making %r" % (index, len(infos), name)
        # This check is for io/rtld-xstat64.os.d,
        # which depends on io/rtld-xstat64.os.dt, which is only temporary.
        if all(os.path.exists(os.path.join(info["cwd"], dep))
               for dep in info["deps"]):
            subprocess.check_call(info["cmd"], shell=True, cwd=info["cwd"])
        else:
            print "SKIP"


if __name__ == "__main__":
    replay(sys.argv[1])
