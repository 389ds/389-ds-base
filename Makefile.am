SUBDIRS=src
PACKAGE_VERSION = @PACKAGE_VERSION@

EXTRA_DIST=	LICENSE \
		svrcore.spec \
		svrcore.pc.in \
		autogen.sh \
		INSTALL.win

pkgconfigdir= $(libdir)/pkgconfig
pkgconfig_DATA= svrcore.pc

# if distdir is a git tag, use that for the git archive tag, else
# just assume a developer build and use HEAD
git-archive:
	if [ -n "$(SRCDISTDIR)" -a -d "$(SRCDISTDIR)" ] ; then \
			srcdistdir=$(SRCDISTDIR) ; \
	else \
			srcdistdir=`pwd` ; \
	fi ; \
	cd $(srcdir) ; \
	if git show-ref --tags -q $(distdir) ; then \
			gittag=$(distdir) ; \
	else \
			gittag=HEAD ; \
	fi ; \
	git archive --prefix=$(distdir)/ $$gittag | bzip2 > $$srcdistdir/$(distdir).tar.bz2

RPMBUILD ?= $(abs_builddir)/rpmbuild

rpmroot:
	$(MKDIR_P) $(RPMBUILD)/BUILD
	$(MKDIR_P) $(RPMBUILD)/RPMS
	$(MKDIR_P) $(RPMBUILD)/SOURCES
	$(MKDIR_P) $(RPMBUILD)/SPECS
	$(MKDIR_P) $(RPMBUILD)/SRPMS

rpmbrprep: git-archive rpmroot
	cp $(distdir).tar.bz2 $(RPMBUILD)/SOURCES
	sed -e "s/__VERSION__/$(PACKAGE_VERSION)/" < $(srcdir)/svrcore.spec > $(RPMBUILD)/SPECS/svrcore.spec

# Requires rpmdevtools. Consider making this a dependancy of rpms.
rpmsources: rpmbrprep
	spectool -g -S -C $(RPMBUILD)/SOURCES $(RPMBUILD)/SPECS/svrcore.spec

rpms: rpmbrprep
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" -ba SPECS/svrcore.spec

srpm: rpmbrprep
	cd $(RPMBUILD); \
	rpmbuild --define "_topdir $(RPMBUILD)" \
			 -bs SPECS/svrcore.spec

