# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
# 
#
# Gmakefile for Directory Server Gateway manual files.
#

LDAP_SRC = ../../../..
BUILD_ROOT = ../../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../../dsgw_include.mk

HTMLDEST   = $(DSGW_MAN_RELDIR)

HTML = a.gif add.htm attribua.gif attribut.htm auth.htm \
	contents.html intro.htm mod.htm n.gif \
	objclass.htm search.htm t.gif y.gif index.map

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

ifeq ($(ARCH), WINNT)
CP2=cmd /c 'sh ../../../../cm/nbsp2utf8.sh $< >'
else
CP2=sh ../../../../cm/nbsp2utf8.sh $< >
endif

all: $(HTMLDEST) $(BINS) 
install: $(HTMLDEST) $(BINS) 

clean:
	$(RM) $(BINS)

$(HTMLDEST)/%.map: %.map
	@-$(RM) $@
	cp $< $@

$(HTMLDEST)/%.gif: %.gif
	@-$(RM) $@
	cp $< $@

$(HTMLDEST)/%: %
	-@$(RM) $@
	$(CP2) $@

strip:	
depend:
