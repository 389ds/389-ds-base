.PHONY: build install test rpms srpms

LIB389_VERS ?= $(shell cat ./VERSION | head -n 1)

all: build

build:
	python setup.py build

install:
	python setup.py install --force --root=/

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

# Needs to generally be run as sudo
test:
	py.test -s ./lib389/tests

