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
MCOM_ROOT = ../../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../dsgw_include.mk

HTML= $(wildcard *.html)

ifeq ($(BUILD_MODULE), HTTP_ADMIN)
HTMLDEST   = $(OBJDIR)/user-forms/html
else
HTMLDEST = $(DSGW_HTML_RELDIR)
endif

DEFINES	+= $(DEFS) 

CFLAGS	+= $(INCLUDES) $(DEFINES) $(ACFLAGS)

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

ifeq ($(BUILD_MODULE), HTTP_ADMIN)
all: $(HTMLDEST) $(BINS)

$(HTMLDEST):
	mkdir -p $(HTMLDEST)

else
all: $(HTMLDEST)
endif

clean: 	clean-manual clean-info
	$(RM) $(BINS)

$(HTMLDEST)/%.htm: %.htm
	cp $< $(HTMLDEST)/$*.htm

$(HTMLDEST)/%.html: %.html
	cp $< $(HTMLDEST)/$*.html

$(HTMLDEST)/%.gif: %.gif
	cp $< $(HTMLDEST)/$*.gif

strip:
depend:
