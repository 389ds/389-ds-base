#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright © 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright © 2001 Netscape Communications Corp.
# All rights reserved.
# 
#
# Gmakefile for Directory Server Gateway html files.
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsdefs.mk

include ../dsgw_include.mk

HTMLDEST   = $(DSGW_HTML_RELDIR)

HTML= auth.html authroot.html authtitle.html csearchtitle.html greeting.html \
	index.html maintitle.html newentrytitle.html searchtitle.html \
	back.gif back1.gif blankbut.gif content.gif content1.gif country.gif \
	exit1.gif forward1.gif group.gif index1.gif netscape.gif \
	organization.gif orgunit.gif person.gif title.gif triangle.gif

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

# install: $(HTMLDEST) $(BINS) inst-manual inst-info
install: 

all:  install

clean: 	
	$(RM) $(BINS)

$(HTMLDEST)/%: %
	-@$(RM) $@
	cp $< $@

$(HTMLDEST)/%.gif: %.gif
	-@$(RM) $@
	cp $< $@

strip:
depend:

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
