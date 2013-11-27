RPMBUILD ?= $(PWD)/rpmbuild
RPM_VERSION ?= $(shell $(PWD)/rpm/rpmverrel.sh version)
RPM_RELEASE ?= $(shell $(PWD)/rpm/rpmverrel.sh release)
PACKAGE = 389-ds-base
RPM_NAME_VERSION = $(PACKAGE)-$(RPM_VERSION)
TARBALL = $(RPM_NAME_VERSION).tar.bz2

clean:
	rm -rf dist
	rm -rf rpmbuild

local-archive:
	-mkdir -p dist/$(RPM_NAME_VERSION)
	rsync -a --exclude=dist --exclude=.git --exclude=rpmbuild . dist/$(RPM_NAME_VERSION)

tarballs: local-archive
	-mkdir -p dist/sources
	cd dist; tar cfj sources/$(TARBALL) $(RPM_NAME_VERSION)
	rm -rf dist/$(RPM_NAME_VERSION)

rpmroot:
	rm -rf $(RPMBUILD)
	mkdir -p $(RPMBUILD)/BUILD
	mkdir -p $(RPMBUILD)/RPMS
	mkdir -p $(RPMBUILD)/SOURCES
	mkdir -p $(RPMBUILD)/SPECS
	mkdir -p $(RPMBUILD)/SRPMS

rpmdistdir:
	mkdir -p dist/rpms

srpmdistdir:
	mkdir -p dist/srpms

rpmbuildprep:
	cp dist/sources/$(TARBALL) $(RPMBUILD)/SOURCES/
	cp rpm/$(PACKAGE)-* $(RPMBUILD)/SOURCES/
	sed -e s/__VERSION__/$(RPM_VERSION)/ -e s/__RELEASE__/$(RPM_RELEASE)/ \
		rpm/$(PACKAGE).spec.in > $(RPMBUILD)/SPECS/$(PACKAGE).spec

srpms: rpmroot srpmdistdir tarballs rpmbuildprep
	rpmbuild --define "_topdir $(RPMBUILD)" -bs $(RPMBUILD)/SPECS/$(PACKAGE).spec
	cp $(RPMBUILD)/SRPMS/$(RPM_NAME_VERSION)-*.src.rpm dist/srpms/
	rm -rf $(RPMBUILD)

patch_srpms: rpmroot srpmdistdir tarballs rpmbuildprep
	cp rpm/*.patch $(RPMBUILD)/SOURCES/
	rpm/add_patches.sh rpm $(RPMBUILD)/SPECS/$(PACKAGE).spec
	rpmbuild --define "_topdir $(RPMBUILD)" -bs $(RPMBUILD)/SPECS/$(PACKAGE).spec
	cp $(RPMBUILD)/SRPMS/$(RPM_NAME_VERSION)-*.src.rpm dist/srpms/
	rm -rf $(RPMBUILD)

rpms: rpmroot srpmdistdir rpmdistdir tarballs rpmbuildprep
	rpmbuild --define "_topdir $(RPMBUILD)" -ba $(RPMBUILD)/SPECS/$(PACKAGE).spec
	cp $(RPMBUILD)/RPMS/*/$(RPM_NAME_VERSION)-*.rpm dist/rpms/
	cp $(RPMBUILD)/RPMS/*/$(PACKAGE)-*-$(RPM_VERSION)-*.rpm dist/rpms/
	cp $(RPMBUILD)/SRPMS/$(RPM_NAME_VERSION)-*.src.rpm dist/srpms/
	rm -rf $(RPMBUILD)

patch_rpms: rpmroot srpmdistdir rpmdistdir tarballs rpmbuildprep
	cp rpm/*.patch $(RPMBUILD)/SOURCES/
	rpm/add_patches.sh rpm $(RPMBUILD)/SPECS/$(PACKAGE).spec
	rpmbuild --define "_topdir $(RPMBUILD)" -ba $(RPMBUILD)/SPECS/$(PACKAGE).spec
	cp $(RPMBUILD)/RPMS/*/$(RPM_NAME_VERSION)-*.rpm dist/rpms/
	cp $(RPMBUILD)/RPMS/*/$(PACKAGE)-*-$(RPM_VERSION)-*.rpm dist/rpms/
	cp $(RPMBUILD)/SRPMS/$(RPM_NAME_VERSION)-*.src.rpm dist/srpms/
	rm -rf $(RPMBUILD)
