
import os
import shutil
import subprocess
import tempfile
import unittest

import replay


def write_file(filename, data):
    fh = open(filename, "w")
    try:
        fh.write(data)
    finally:
        fh.close()


def read_file(filename):
    fh = open(filename)
    try:
        return fh.read()
    finally:
        fh.close()


# From http://lackingrhoticity.blogspot.com/2008/11/tempdirtestcase-python-unittest-helper.html
class TempDirTestCase(unittest.TestCase):

    def setUp(self):
        self._on_teardown = []

    def make_temp_dir(self):
        temp_dir = tempfile.mkdtemp(prefix="tmp-%s-" % self.__class__.__name__)
        def tear_down():
            shutil.rmtree(temp_dir)
        self._on_teardown.append(tear_down)
        return temp_dir

    def tearDown(self):
        for func in reversed(self._on_teardown):
            func()


here_dir = os.path.abspath(os.path.dirname(__file__))


class Test(TempDirTestCase):

    def test(self):
        temp_dir = self.make_temp_dir()
        write_file(os.path.join(temp_dir, "Makefile"), """
all: foo.o bar.o
%.o: %.c
\t @echo "compiled `cat $<`" > $@
""")
        write_file(os.path.join(temp_dir, "foo.c"), "file foo")
        write_file(os.path.join(temp_dir, "bar.c"), "file bar")
        log_file = os.path.join(temp_dir, "log")
        subprocess.check_call(["env", "MAKE_LOG=%s" % log_file,
                               os.path.join(here_dir, "wrapper.py")],
                              cwd=temp_dir)
        write_file(os.path.join(temp_dir, "foo.c"), "secondary foo")
        # Rebuild without running make
        replay.replay(log_file)
        self.assertEquals(read_file(os.path.join(temp_dir, "foo.o")),
                          "compiled secondary foo\n")


if __name__ == "__main__":
    unittest.main()
