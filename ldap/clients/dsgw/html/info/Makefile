#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright © 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright © 2001 Netscape Communications Corp.
# All rights reserved.
# 
#
# Gmakefile for Directory Server Gateway info files.
#

LDAP_SRC = ../../../..
MCOM_ROOT = ../../../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../../dsgw_include.mk

HTMLDEST   = $(DSGW_INFO_RELDIR)

HTML = infonav.html 

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

all: $(HTMLDEST) $(BINS) 
install: $(HTMLDEST) $(BINS) 

clean:
	$(RM) $(BINS)

$(HTMLDEST)/%: %
	-@$(RM) $@
	cp $< $@

strip:	
depend:
