# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import string
import os
import sys
import pytest
from tempfile import mkdtemp
from shutil import rmtree

MAJOR, MINOR, _, _, _ = sys.version_info


def ldrop(numchar, inputstr):
    """
        Drop numchar characters from the beginning of each line in inputstring.
    """
    if MAJOR >= 3:
        return "\n".join(map(lambda x: x[numchar:], inputstr.split("\n")))
    else:
        return "\n".join([x[numchar:] for x in string.split(inputstr, "\n")])


@pytest.fixture
def fake_ds_class():
    """
        Returns fake DSInstance-like class
    """
    class FakeDSInstance(object):
        def return_one(self):
            return 1

    return FakeDSInstance


@pytest.fixture(scope="module")
def fake_ds_modules(request):
    """
        Returns path to directory containing fake modules.
    """

    # module_dir contains fake ds modules, that can be used for testing
    module_dir = mkdtemp()
    old_cwd = os.getcwd()

    # when fixture is destroyed, we change back to old cwd
    # and delete fake module dir
    def fin():
        os.chdir(old_cwd)
        rmtree(module_dir)
    request.addfinalizer(fin)

    # make module_dir a python package
    with open(module_dir + "/__init__.py", 'a'):
        os.utime(module_dir + "/__init__.py", None)

    # create module module_dir/repl.py with few functions
    with open(module_dir + "/repl.py", "w") as repl_file:
        repl_module = """
            def with_no_arg(ds):
                return ds

            def with_one_arg(ds, arg):
                return ds, arg

            def return_argument_given(ds, arg):
                return arg

            def with_kwarg(ds, kwarg=2):
                return kwarg

            def with_all(ds, arg1, arg2, kwarg1=1, kwarg2=2):
                return arg1, arg2, kwarg1, kwarg2

            """
        # Drop 12 chars from left and write to file
        repl_file.write(ldrop(12, repl_module))

    os.mkdir(module_dir + "/plugin")

    # make module_dir/plugin a python package
    with open(module_dir + "/plugin/__init__.py", 'a'):
        os.utime(module_dir + "/plugin/__init__.py", None)

    # create module module_dir/plugin/automember.py with few functions
    with open(module_dir + "/plugin/automember.py", "w") as automem_file:
        automember_module = """
            def with_no_arg(ds):
                return ds

            def with_one_arg(ds, arg):
                return ds, arg

            def calling_ds_method(ds, arg):
                return (ds, ds.return_one(), arg)

            def calling_repl(ds, arg1, arg2):
                return ds.repl.return_argument_given(1) + arg1 + arg2

            def calling_repl_with_kwarg2(ds):
                return ds.repl.with_kwarg()

            def calling_repl_with_kwarg1(ds):
                return ds.repl.with_kwarg(kwarg=1)

            def with_all(ds, arg1, arg2, kwarg1=1, kwarg2=2):
                a, b, c, d = ds.repl.with_all(1, 4, kwarg1=3)
                return a + arg1, b + arg2, c + kwarg1, d + kwarg2

            """
        # Drop 12 chars from left and write to file
        automem_file.write(ldrop(12, automember_module))

    os.chdir(os.path.dirname(module_dir))
    sys.path.append(os.path.dirname(module_dir))
    return module_dir
