# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
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

HTMLDEST   = $(DSGW_CONF_RELDIR)

HTML =	dsgw.tmpl dsgwfilter.conf dsgwsearchprefs.conf \
	dsgw-l10n.conf \
	search.html searchString.html \
	csearch.html csearchAttr.html csearchBase.html csearchMatch.html csearchString.html csearchType.html \
	newentry.html newentryType.html newentryName.html \
	authSearch.html authPassword.html \
	display-country.html display-group.html display-groupun.html \
	display-org.html display-orgperson.html \
	display-orgunit.html display-person.html \
	display-ntperson.html display-dnedit.html display-dneditpeople.html \
	display-ntgroup.html display-dc.html \
	edit-passwd.html \
	list-Anything.html list-fa-People.html list-fa-Groups.html \
	list-Auth.html list-Groups.html list-Org-Units.html \
	list-Organizations.html list-People.html list-urlsearch.html \
	list-NT-People.html list-NT-Groups.html list-Domaincomponent.html

BINS=$(addprefix $(HTMLDEST)/,$(HTML))

LANGFILES=$(wildcard $(DSGW_DEFAULT_LANG)/dsgwcollate.*) $(wildcard $(DSGW_DEFAULT_LANG)/dsgw-l10n.*)

LANGDEST=$(addprefix $(DSGW_CONF_RELDIR)/, $(LANGFILES))

ifeq ($(ARCH), WINNT)
CP2=cmd /c 'sh ../../../cm/nbsp2utf8.sh $< > $@'
else
CP2=sh ../../../cm/nbsp2utf8.sh $< > $@
endif

all: $(HTMLDEST) $(BINS) $(HTMLDEST)/$(DSGW_DEFAULT_LANG) $(LANGDEST)

install: $(HTMLDEST) $(BINS) 

clean:
	$(RM) $(BINS)

$(HTMLDEST)/$(DSGW_DEFAULT_LANG)/%: $(DSGW_DEFAULT_LANG)/%
	echo $(LANGDEST)
	-@$(RM) $@
	$(CP2)

$(HTMLDEST)/%: %
	echo $(LANGDEST)
	-@$(RM) $@
	$(CP2)

$(HTMLDEST)/$(DSGW_DEFAULT_LANG)/dsgwcollate.conf: $(DSGW_DEFAULT_LANG)/dsgwcollate.conf
	echo $(LANGDEST)
	-@$(RM) $@
	$(CP2)

strip:
depend:
