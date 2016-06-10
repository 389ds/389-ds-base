.PHONY: build install test rpms srpms

LIB389_VERS ?= $(shell cat ./VERSION | head -n 1)
PYTHON ?= /usr/bin/python

all: build

build:
	$(PYTHON) setup.py build

install:
	$(PYTHON) setup.py install --force --root=/

rpmbuild-prep:
	mkdir -p ./dist/
	mkdir -p ~/rpmbuild/SOURCES
	mkdir -p ~/rpmbuild/SPECS
	git archive --prefix=python-lib389-$(LIB389_VERS)-1/ HEAD | bzip2 > ./dist/python-lib389-$(LIB389_VERS)-1.tar.bz2
	cp ./dist/python-lib389-$(LIB389_VERS)-1.tar.bz2 ~/rpmbuild/SOURCES/

srpm: rpmbuild-prep
	rpmbuild -bs python-lib389.spec
	cp ~/rpmbuild/SRPMS/python-lib389*.src.rpm ./dist/

rpm: rpmbuild-prep
	rpmbuild -bb python-lib389.spec

pep8:
	pep8 --max-line-length=160 ./lib389
# Needs to generally be run as sudo
test:
	py.test -s ./lib389/tests

