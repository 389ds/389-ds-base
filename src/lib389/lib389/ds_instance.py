# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import re
import sys
from collections.abc import Callable


class DSDecorator(object):
    """ Implements decorator that can be instantiated during runtime.

        Accepts function and instance of DSInstance. When called,
        it will call passed function with instance of DSInstance
        passed as the first argument.
    """
    def __init__(self, fun, ds_instance):
        self.fun = fun
        self.ds_instance = ds_instance

    def __call__(self, *args, **kwargs):
        if args is None and kwargs is None:
            return self.fun(self.ds_instance)
        elif args is not None and kwargs is None:
            return self.fun(self.ds_instance, *args)
        elif args is None and kwargs is not None:
            return self.fun(self.ds_instance, **kwargs)
        else:
            return self.fun(self.ds_instance, *args, **kwargs)


class DSModuleProxy(object):
    """ Proxy with DS-decorated functions from modules in directory.

        DSModuleProxy object acts as a proxy to functions defined in modules
        stored in directory. Proxy itself can have other proxies as
        attributes, so returned object follows the structure of passed
        directory. All funcions from all modules are decorated with
        DSDecorator - DSInstance object will be passed to them as first
        argument.

        Kudos to Milan Falesnik <mfalesni@redhat.com>
    """

    def __init__(self, module_name):
        self.name = module_name

    @classmethod
    def populate_with_proxies(cls, ds, obj, directory):
        """
            Returns a proxy with decorated functions from modules in directory.
        """
        if not os.path.isdir(directory):
            raise RuntimeError("Last argument %s was not directory" %
                               directory)
        for item in os.listdir(directory):

            # case when item is directory
            if os.path.isdir(directory + "/" + item):
                # Recursively call self on all subdirectories
                proxy = cls(item)
                cls.populate_with_proxies(ds, proxy, directory + "/" + item)
                setattr(obj, item, proxy)

            # case when item is python module
            elif (os.path.isfile(directory + "/" + item) and
                    re.match(r'(.*\/)?[^_/].*[^_/]\.py$', item)):

                # get the module object corresponding to processed file
                item = item.replace('.py', '')
                to_import = (directory + "/" + item).replace('/', '.')
                __import__(to_import)
                module = sys.modules[to_import]
                proxy = cls(item)

                # for each function from module create decorated one
                for attr in dir(module):
                    fun = getattr(module, attr)
                    if isinstance(fun, Callable):
                        decorated = DSDecorator(fun, ds)
                        setattr(proxy, attr, decorated)
                setattr(obj, item, proxy)

            else:
                # skip anything that is not directory or python module
                pass


class DSInstance(object):

    def __init__(self):
        DSModuleProxy.populate_with_proxies(self, self, 'dsmodules')
