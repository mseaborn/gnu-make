
from distutils.core import setup, Extension

make_c_files = [
    "ar.c", "arscan.c",
    "commands.c", "default.c", "dir.c", "expand.c", "file.c",
    "function.c", "getopt.c", "getopt1.c", "implicit.c", "job.c", "main.c",
    "misc.c", "read.c", "remake.c", "remote-stub.c", "rule.c", "signame.c",
    "strcache.c", "variable.c", "version.c", "vpath.c", "hash.c",
    # Python bits
    "ext.c"]

setup(name="gnumake",
      ext_modules=[Extension(
            "gnumake",
            include_dirs=["."],
            define_macros=[("LOCALEDIR", '"/usr/local/share/locale"'),
                           ("INCLUDEDIR", '"/usr/local/include"'),
                           ("LIBDIR", '"/usr/local/lib"')],
            libraries=["rt"],
            # extra_link_args=["-Wl,-z,defs"],
            sources=make_c_files,
            )])
