# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
# 
#
# Gmakefile for Directory Server Gateway html files.
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../dsgw_include.mk

HTMLDEST   = $(DSGW_HTML_RELDIR)

HTML= auth.html authroot.html authtitle.html csearchtitle.html \
	emptyFrame.html greeting.html index.html maintitle.html \
	newentrytitle.html searchtitle.html style.css \
	transparent.gif back1.gif content1.gif rolodex.gif \
	country.gif exit1.gif forward1.gif group.gif index1.gif \
        left_bottom.gif left_on.gif right_off.gif \
        left_off.gif right_bottom.gif right_on.gif \
	organization.gif orgunit.gif person.gif clear.gif message.gif \
	alert.html alert.gif confirm.html confirm.gif orgicon.gif aim-online.gif

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

ifeq ($(ARCH), WINNT)
CP2=cmd /c 'sh ../../../cm/nbsp2utf8.sh $< >'
else
CP2=sh ../../../cm/nbsp2utf8.sh $< >
endif

include $(BUILD_ROOT)/config/webint.mk

all: $(HTMLDEST) $(BINS) all-manual all-info

install: $(HTMLDEST) $(BINS) inst-manual inst-info

clean: 	clean-manual clean-info
	$(RM) $(BINS)

# gif files (and presumably other binary files) are copied, not filtered
$(HTMLDEST)/%.gif: %.gif
	@-$(RM) $@
	cp $< $@

# all other files are filtered
$(HTMLDEST)/%: %
	@-$(RM) $@
	$(CP2) $@

all-manual:	
	cd manual; $(MAKE) $(MFLAGS) all

all-info:	
	cd info; $(MAKE) $(MFLAGS) all

inst-manual:	
	cd manual; $(MAKE) $(MFLAGS) install

inst-info:	
	cd info; $(MAKE) $(MFLAGS) install 

clean-manual:	
	cd manual; $(MAKE) $(MFLAGS) clean

clean-info:	
	cd info; $(MAKE) $(MFLAGS) clean

strip:
depend:
