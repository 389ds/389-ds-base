.PHONY: build install test rpms srpms

LIB389_VERS ?= $(shell cat ./VERSION | head -n 1)
PYTHON ?= /usr/bin/python
RPMBUILD ?= $(shell pwd)/rpmbuild

all: build

build:
	$(PYTHON) setup.py build

install:
	$(PYTHON) setup.py install --skip-build --force --root=/

rpmbuild-prep:
	mkdir -p ./dist/
	mkdir -p $(RPMBUILD)/SOURCES
	mkdir -p $(RPMBUILD)/SPECS
	git archive --prefix=python-lib389-$(LIB389_VERS)-1/ HEAD | bzip2 > ./dist/python-lib389-$(LIB389_VERS)-1.tar.bz2
	cp ./dist/python-lib389-$(LIB389_VERS)-1.tar.bz2 $(RPMBUILD)/SOURCES/

srpm: rpmbuild-prep
	rpmbuild --define "_topdir $(RPMBUILD)" -bs python-lib389.spec

rpm: rpmbuild-prep
	rpmbuild --define "_topdir $(RPMBUILD)" -bb python-lib389.spec

pep8:
	pep8 --max-line-length=160 ./lib389
# Needs to generally be run as sudo
test:
	py.test -s ./lib389/tests

