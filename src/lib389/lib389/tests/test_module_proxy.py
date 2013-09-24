# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
from lib389.ds_instance import DSModuleProxy


def test_module_proxy_fun_with_no_arg(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.repl.with_no_arg() == ds_inst


def test_module_proxy_fun_with_one_arg(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.repl.with_one_arg(1) == (ds_inst, 1)


def test_module_proxy_plugin_fun_with_no_arg(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.plugin.automember.with_no_arg() == ds_inst


def test_module_proxy_plugin_fun_with_one_arg(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.plugin.automember.with_one_arg(2) == (ds_inst, 2)


def test_module_proxy_plugin_call_ds_method(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.plugin.automember.calling_ds_method(2) == (ds_inst, 1, 2)


def test_module_proxy_plugin_call_another_module(fake_ds_class,
                                                 fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename(fake_ds_modules))
    assert ds_inst.plugin.automember.calling_repl(2, 3) == 6


def test_module_proxy_plugin_call_another_module_kwarg(fake_ds_class,
                                                       fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename
                                        (fake_ds_modules))
    assert ds_inst.plugin.automember.calling_repl_with_kwarg1() == 1
    assert ds_inst.plugin.automember.calling_repl_with_kwarg2() == 2


def test_module_proxy_plugin_call_with_all(fake_ds_class, fake_ds_modules):
    ds_inst = fake_ds_class()
    DSModuleProxy.populate_with_proxies(ds_inst, ds_inst,
                                        os.path.basename
                                        (fake_ds_modules))
    assert ds_inst.plugin.automember.with_all(0, 3, 2, 1) == (1, 7, 5, 3)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mo     de
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
