# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
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
