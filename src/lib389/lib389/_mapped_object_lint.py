# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from functools import partial
from inspect import signature
from typing import (
    Callable,
    List,
    Optional,
    Tuple,
    Union,
    Type,
    Generator,
    Any
)
from lib389._constants import DIRSRV_STATE_ONLINE
from lib389.utils import ensure_bytes, ensure_str, ensure_list_str, ensure_int


DSLintSpec = Tuple[str, Callable]
DSLintParsedSpec = Tuple[Optional[str], Optional[str]]
DSLintClassSpec = Generator[DSLintSpec, None, None]
DSLintMethodSpec = Union[str, None, Type[List]]
DSLintResults = Generator[Any, None, None]


class DSLint():
    """In a super-class, create a method with name beginning with `_lint_`
    which would yield results (as described below). Such a method will
    then be available to the `lint()` method of the class.

    `lint_list`: takes a spec and yields available lints, recursively
    `lint`:      takes a spec and runs lints according to it, yielding results if any

    `spec`: is a colon-separated string, with prefix matching a method name and suffix
            being passed down to the method.

    A class inheriting from hereby class shall implement a method named `lint_uid()` which
    returns a pretty name of the object. This is to be used by a higher level code.

    Each lint method has to have a name prefix with _lint_. It may accept an optional
    parameter `spec` in which case:
    - it has to accept typing.List class as a parameter, in which case it shall yield
      all possible lint specs for that method
    - it receives the suffix provided to the `spec` of hereby `lint` method (as mentioned above)

    This means that we can detect and report common administrative errors
    in the server from our cli and rest tools.

    The structure of a result shall be:

        {
        dsle: '<identifier>'. dsle == ds lint error. Will be a code unique to
                            this module for the error, IE DSBLE0001.
        severity: '[HIGH:MEDIUM:LOW]'. severity of the error.
        items: '(dn,dn,dn)'. List of affected DNs or names.
        detail: 'msg ...'. An explination of the error.
        fix: 'msg ...'. Steps to resolve the error.
        }
    """

    @classmethod
    def _dslint_fname(cls, method: Callable) -> Optional[str]:
        """Return a pretty name for a method."""
        if callable(method) and method.__name__.startswith('_lint_'):
            return method.__name__[len('_lint_'):]
        else:
            return None

    @staticmethod
    def _dslint_parse_spec(spec: Optional[str]) -> DSLintParsedSpec:
        """Split `spec` to prefix and suffix."""
        wanted, *rest = spec.split(':', 1) if spec else (None, None)
        return (wanted if wanted not in [None, '*'] else None,
                rest[0] if rest else None)

    @classmethod
    def _dslint_make_spec(cls, method: Callable, spec: Optional[str] = None) -> str:
        """Build a new spec from prefix (`method` name) and suffix (`spec`)."""
        fname = cls._dslint_fname(method)
        return f'{fname}:{spec}' if spec else fname

    def lint_list(self, spec: Optional[str] = None) -> DSLintClassSpec:
        """Yield specs the object provides.

        This yields from each lint method yielding all specs it can provide.
        """

        assert hasattr(self, 'lint_uid')

        # Find _lint_ methods
        # NOTE: There is a caveat: don't you dare try to getattr on a @property, or
        #       you get it executed. That's why the following line's complexity.
        fs = [getattr(self, f) for f in dir(self)
              if f.startswith('_lint_') and self._dslint_fname(getattr(self, f))]

        # Filter acording to the `spec`
        wanted, rest = self._dslint_parse_spec(spec)
        if wanted:
            try:
                fs = [next(filter(lambda f: self._dslint_fname(f) == wanted, fs))]
            except StopIteration:
                raise ValueError('there is no such lint function')

        # Yield known specs
        for f in fs:
            fspec_t = signature(f).parameters.get('spec', None)
            if fspec_t:
                assert fspec_t.annotation == DSLintMethodSpec
                for fspec in [rest] if rest else f(spec=List):
                    yield self._dslint_make_spec(f, fspec), partial(f, spec=fspec)
            else:
                yield self._dslint_make_spec(f, rest), f

    def lint(self, spec: DSLintMethodSpec = None) -> DSLintResults:
        """Lint the object according to the `spec`."""

        if spec == List:
            yield from self.lint_list()
        else:
            for fn, f in self.lint_list(spec):
                yield from f()


class DSLints():
    """This is a meta class to provide lint functionality to classes that provide
    method `list` which returns list of objects that inherit from DSLint.

    Calling `lint` or `lint_list` method yields from respective object's methods.

    The `spec` is a colon-separated string. Its prefix matches the respective object's
    `lint_uid` (or all when asterisk); the suffix is passed down to the respective
    object's method.
    """

    def lint_list(self, spec: Optional[str] = None) -> DSLintClassSpec:
        """Yield specs the objects returned by `list` method provide."""

        assert hasattr(self, 'list')

        # Filter acording to the `spec`
        wanted, rest_spec = DSLint._dslint_parse_spec(spec)
        if wanted in [None, '*']:
            clss = self.list()
        else:
            clss = (cls for cls in self.list() if cls.lint_uid() == wanted)

        # Yield known specs
        for cls in clss:
            for fn, f in cls.lint_list(spec=rest_spec):
                yield (f'{cls.lint_uid()}:{fn}',
                       partial(f, rest_spec) if rest_spec else f)

    def lint(self, spec: DSLintMethodSpec = None) -> DSLintResults:
        """Lint the objects returned by `list` method according to the `spec`."""

        if spec == List:
            yield from self.lint_list()
        else:
            if ":" in spec:
                # Skip over any decorators, and get the actual function name
                check_name = spec.split(":")[-1]
            else:
                check_name = spec
            for obj in self.list():
                yield from obj.lint(check_name)


def _cached_dse_ldif_for_lint(instance):
    """Return a cached :class:`lib389.dseldif.DSEldif` for local ``dse.ldif`` reads in lint code."""
    if not getattr(instance, 'isLocal', False):
        return None
    if getattr(instance, '_lib389_dse_lint_cache', None) is not None:
        return instance._lib389_dse_lint_cache
    if getattr(instance, '_lib389_dse_lint_failed', False):
        return None
    try:
        from lib389.dseldif import DSEldif
        dse = DSEldif(instance)
    except (OSError, IOError, EnvironmentError):
        instance._lib389_dse_lint_failed = True
        return None
    instance._lib389_dse_lint_cache = dse
    return dse


def lint_get_attr_val_utf8(obj, key):
    """Like :meth:`DSLdapObject.get_attr_val_utf8`, but read ``dse.ldif`` when the server is down."""
    if obj._instance.state == DIRSRV_STATE_ONLINE:
        return obj.get_attr_val_utf8(key)
    dse = _cached_dse_ldif_for_lint(obj._instance)
    if dse is None:
        raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")

    vals = dse.get(ensure_str(obj._dn).lower(), ensure_str(key), single=False, lower=True)
    if vals is None:
        return None
    if len(vals) == 0:
        return ''
    return ensure_str(vals[0])


def lint_get_attr_val_utf8_l(obj, key):
    from lib389.utils import ensure_str
    v = lint_get_attr_val_utf8(obj, key)
    if v is None:
        return None
    return ensure_str(v).lower()


def lint_get_attr_vals_utf8(obj, key):
    """Like :meth:`DSLdapObject.get_attr_vals_utf8` for one attribute, with offline ``dse.ldif`` support."""
    if obj._instance.state == DIRSRV_STATE_ONLINE:
        return obj.get_attr_vals_utf8(key)
    dse = _cached_dse_ldif_for_lint(obj._instance)
    if dse is None:
        raise ValueError("Invalid state. Cannot get properties on instance that is not ONLINE")
    vals = dse.get(ensure_str(obj._dn).lower(), ensure_str(key), single=False, lower=True)
    if vals is None:
        return []
    return ensure_list_str(vals)


def lint_get_attr_vals_utf8_l(obj, key):
    from lib389.utils import ensure_str
    return [ensure_str(x).lower() for x in lint_get_attr_vals_utf8(obj, key)]


def lint_get_attr_val(obj, key):
    """Like :meth:`DSLdapObject.get_attr_val` (bytes), with offline ``dse.ldif`` support."""
    if obj._instance.state == DIRSRV_STATE_ONLINE:
        return obj.get_attr_val(key)
    s = lint_get_attr_val_utf8(obj, key)
    if s is None:
        return None
    if s == '':
        return ''
    return ensure_bytes(s)


def lint_get_attr_val_int(obj, key):
    if obj._instance.state == DIRSRV_STATE_ONLINE:
        return obj.get_attr_val_int(key)
    v = lint_get_attr_val_utf8(obj, key)
    if v == '' or v is None:
        return None
    return ensure_int(v)


def lint_plugin_enabled(plugin):
    """Whether ``nsslapd-pluginEnabled`` is ``on``; works offline via ``dse.ldif``."""
    return lint_get_attr_val_utf8(plugin, 'nsslapd-pluginEnabled') == 'on'
