# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
# 
#
# Gmakefile for Directory Server Gateway info files.
#

LDAP_SRC = ../../../..
BUILD_ROOT = ../../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsconfig.mk
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
