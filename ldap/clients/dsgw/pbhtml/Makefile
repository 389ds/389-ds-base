# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
# 
#
# Gmakefile for Directory Server Phonebook html files.
#                                       1/21/98 - RJP
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDSTRIP=true # don't let nsconfig.mak define target strip
NOSTDCLEAN=true # don't let nsconfig.mak define target clean
NOSTDDEPEND=true # don't let nsconfig.mak define target depend

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

include ../dsgw_include.mk

HTMLDEST   = $(DSGW_PBHTML_RELDIR)

HTML=   emptyFrame.html index.html 16-conference.gif \
	office.gif report.html 16-person.gif conference.gif \
	pbrd.jpg department.gif person.gif clear.gif \
	vendor.gif style.css intro.html phone.html view_vcard.gif \
	phone.js view_vcard_sm.gif \
	rolodex.gif get_cert.gif \
	carded.html get_cert_sm.gif pixel.gif \
	32-office.gif 32-alert.gif 32-conference.gif 32-person.gif \
	32-message.gif nullStringError.html tiny_cert.gif \
	tiny_vcard.gif confirm.html alert.html orgicon.gif aim-online.gif

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

include $(BUILD_ROOT)/config/webint.mk

all: $(HTMLDEST) $(BINS)

install: $(HTMLDEST) $(BINS)

clean: 	
	$(RM) $(BINS)

$(HTMLDEST)/%: %
	@-$(RM) $@
	cp $< $@

strip:
depend:
