# build_hooks.py
"""Custom cmdclass wrappers needed by pyproject.toml."""

from pathlib import Path
import sys

from setuptools.command.build_py import build_py as _orig_build_py
from build_manpages import build_manpages, install


class build_py(_orig_build_py):
    """Run normal build_py, then make the built tree importable, then man pages."""

    def run(self):
        # 1. copy src/… to build/lib/
        super().run()

        # 2. prepend the freshly-built package dir to sys.path
        build_lib = Path(self.build_lib).resolve()
        if str(build_lib) not in sys.path:
            sys.path.insert(0, str(build_lib))

        # 3. generate the manual pages
        self.run_command("build_manpages")


# expose the other two commands unchanged so we can reference them
build_manpages = build_manpages
install        = install

