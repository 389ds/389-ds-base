# build_hooks.py
"""Custom cmdclass wrappers needed by pyproject.toml."""

from pathlib import Path
from shutil import copyfile
import sys

from setuptools.command.build_py import build_py as _orig_build_py
from setuptools.command.egg_info import egg_info as _orig_egg_info
from build_manpages import build_manpages, install


def prepare_topologies():
    """Refresh the fixture copy in a checkout, or use the bundled sdist copy."""
    package_root = Path(__file__).resolve().parent
    source = package_root.parent.parent / "dirsrvtests/lib/test389/topologies.py"
    target = package_root / "lib389/tests/topologies.py"
    if source.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, target)
    elif not target.is_file():
        raise FileNotFoundError(
            "Cannot package lib389: neither dirsrvtests/lib/test389/topologies.py "
            "nor the bundled lib389/tests/topologies.py is available"
        )


class egg_info(_orig_egg_info):
    """Generate fixtures before setuptools collects the source archive files."""

    def run(self):
        prepare_topologies()
        super().run()


class build_py(_orig_build_py):
    """Run normal build_py, then make the built tree importable, then man pages."""

    def run(self):
        prepare_topologies()
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

