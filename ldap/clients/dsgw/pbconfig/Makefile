#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright © 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright © 2001 Netscape Communications Corp.
# All rights reserved.
# 
#
# Gmakefile for Directory Server Gateway config files.
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../dsgw_include.mk

HTMLDEST   = $(DSGW_PBCONF_RELDIR)

HTML =	pb.tmpl dsgwsearchprefs.conf display-room.html edit-passwd.html\
	display-orgperson.html list-Auth.html authPassword.html \
	list-People.html authSearch.html display-orgunit.html \
	dsgwfilter.conf

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

ifeq ($(ARCH), WINNT)
CP2=cmd /c 'sh ../../../cm/nbsp2utf8.sh $< >'
else
CP2=sh ../../../cm/nbsp2utf8.sh $< >
endif

all: $(HTMLDEST) $(BINS) 

install: $(HTMLDEST) $(BINS) 

clean:
	$(RM) $(BINS)

$(HTMLDEST)/%: %
	-@$(RM) $@
	$(CP2) $@

strip:
depend:
