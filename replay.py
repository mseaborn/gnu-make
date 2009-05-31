#!/usr/bin/env python

import os
import subprocess
import sys


def replay(filename):
    infos = [eval(line, {}) for line in open(filename)]
    infos = [info for info in infos
             if not (info["recmake"]
                     or "wrapper.py" in info["cmd"])]
    for index, info in enumerate(infos):
        name = info["file"]
        topdir = "/work/nacl/2/googleclient/native_client/glibc-2.9/build/"
        if name.startswith(topdir):
            name = name[len(topdir):]
        print "[%i/%i] making %r" % (index, len(infos), name)
        # This check is for io/rtld-xstat64.os.d,
        # which depends on io/rtld-xstat64.os.dt, which is only temporary.
        missing = [
            dep for dep in info["deps"]
            if not (dep["phony"] or
                    os.path.exists(os.path.join(info["cwd"], dep["file"])))]
        if len(missing) == 0:
            if info["noerror"]:
                subprocess.call(info["cmd"], shell=True, cwd=info["cwd"])
            else:
                subprocess.check_call(info["cmd"], shell=True, cwd=info["cwd"])
        else:
            print "SKIP: missing: %s" % missing


if __name__ == "__main__":
    replay(sys.argv[1])
