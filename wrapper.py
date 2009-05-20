#!/usr/bin/env python

import ctypes
import os
import sys
import traceback

import gnumake


class Floc(ctypes.Structure):
    _fields_ = [("filename", ctypes.c_char_p),
                ("lineno", ctypes.c_uint)]

class Commands(ctypes.Structure):
    _fields_ = [("fileinfo", Floc),
                ("commands", ctypes.c_char_p)]

class File(ctypes.Structure):
    def get_deps(self):
        got = []
        dep = self.deps
        while bool(dep):
            got.append(dep.contents)
            dep = dep.contents.next
        return got

class Dep(ctypes.Structure):
    pass

File._fields_ = [("name", ctypes.c_char_p),
                 ("hname", ctypes.c_char_p),
                 ("vpath", ctypes.c_char_p),
                 ("deps", ctypes.POINTER(Dep)),
                 ("commands", ctypes.POINTER(Commands))]

Dep._fields_ = [("next", ctypes.POINTER(Dep)),
                ("name", ctypes.c_char_p),
                ("stem", ctypes.c_char_p),
                ("file", ctypes.POINTER(File))]

class JobChild(ctypes.Structure):
    _fields_ = [("next", ctypes.c_void_p),
                ("file", ctypes.POINTER(File)),
                ("environment", ctypes.c_void_p),
                ("command_lines", ctypes.POINTER(ctypes.c_char_p)),
                ("command_line", ctypes.c_uint),
                ("command_ptr", ctypes.c_char_p)]


def dump_target(file, indent):
    print indent, repr(file.name)
    if file.commands:
        print indent, repr(file.commands.contents.commands), \
            file.commands.contents.fileinfo.lineno
    for dep in file.get_deps():
        dump_target(dep.file.contents, indent + "  ")


log_file = os.getenv("MAKE_LOG")
if log_file is not None:
    log_fh = file(log_file, "a")
else:
    log_fh = None


void = ctypes.c_int
callback_type = ctypes.CFUNCTYPE(
    void, ctypes.POINTER(JobChild), ctypes.c_char_p)


def hook(job, cmd):
    job = job.contents
    target_file = job.file.contents
    dep_files = [dep.file.contents.name for dep in target_file.get_deps()]
    info = {"file": job.file.contents.name,
            "cmd": cmd,
            "cwd": os.getcwd(),
            "deps": dep_files,
            "recmake": cmd.startswith(__file__ + " "),
            }
    if log_fh is not None:
        log_fh.write("%r\n" % info)
        log_fh.flush()
    # ctypes apparently doesn't let us specify void return type.
    return 0


keep = []
def wrap_func(func, ctype):
    # Using ctypes.addressof() gives the wrong function address.
    f = callback_type(func)
    keep.append(f)
    return ctypes.cast(f, ctypes.c_void_p).value

gnumake.set_hook(wrap_func(hook, callback_type))

# Set MAKE so that this wrapper gets called for recursive invocations.
os.environ["MAKE"] = os.path.abspath(sys.argv[0])

gnumake.run(tuple(["make"] + sys.argv[1:]))
