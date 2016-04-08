SUBDIRS=src

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


