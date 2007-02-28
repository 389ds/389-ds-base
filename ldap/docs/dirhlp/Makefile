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
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK

# 
# GNU Makefile for Directory Server Console Help 
#

BUILD_ROOT = ../../..
LDAP_SRC = ../..
MODULE=httpdAdminHTML

NOSTDCLEAN=true # don't let nsconfig.mk define target clean

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

HTMLDEST=$(OBJDIR)/manual/slapd
NEWHTMLDEST=$(OBJDIR)/manual/slapd/help
COPYRIGHTDEST=$(OBJDIR)/manual/slapd/copyright

NOSTDSTRIP=true
NOSTDDEPEND=true

HTML= index.map index.htm pixel.gif

NEWHTMLTOKEN=tokens.map
NEWHTMLTOPIC=topicindex.htm

COPYRIGHTFILES=copyright.html

NEWHTML = account_mgmt.htm \
  adv_search.htm \
  configtab_chaindb.htm \
  configtab_chaindb2.htm \
  configtab_chaindb3.htm \
  configtab_chaindb4.htm \
  configtab_chaindb5.htm \
  configtab_chaindb6.htm \
  configtab_chaindb7.htm \
  configtab_db.htm \
  configtab_db10.htm \
  configtab_db11.htm \
  configtab_db12.htm \
  configtab_db13.htm \
  configtab_db14.htm \
  configtab_db15.htm \
  configtab_db2.htm \
  configtab_db3.htm \
  configtab_db4.htm \
  configtab_db5.htm \
  configtab_db6.htm \
  configtab_db7.htm \
  configtab_db8.htm \
  configtab_db9.htm \
  configtab_ldbmdb.htm \
  configtab_logs.htm \
  configtab_logs2.htm \
  configtab_logs3.htm \
  configtab_maptree.htm \
  configtab_maptree2.htm \
  configtab_maptree3.htm \
  configtab_maptree4.htm \
  configtab_maptree5.htm \
  configtab_maptree6.htm \
  configtab_maptree7.htm \
  configtab_plugins.htm \
  configtab_replication.htm \
  configtab_replication2.htm \
  configtab_replication3.htm \
  configtab_replication4.htm \
  configtab_replication5.htm \
  configtab_replication6.htm \
  configtab_replication7.htm \
  configtab_replication8.htm \
  configtab_synchronization1.htm \
  configtab_synchronization2.htm \
  configtab_synchronization3.htm \
  configtab_rootnode.htm \
  configtab_rootnode2.htm \
  configtab_rootnode3.htm \
  configtab_rootnode4.htm \
  configtab_rootnode5.htm \
  configtab_rootnode6.htm \
  configtab_rootnode7.htm \
  configtab_rootnode8.htm \
  configtab_rootnode9.htm \
  configtab_schema.htm \
  configtab_schema2.htm \
  configtab_schema3.htm \
  configtab_schema4.htm \
  configtab_schema5.htm \
  dirtab_cos.htm \
  dirtab_cos2.htm \
  dirtab_cos3.htm \
  dirtab_role.htm \
  dirtab_role2.htm \
  dirtab_role3.htm \
  dirtab_role4.htm \
  dirtab_role5.htm \
  dirtab_role6.htm \
  dirtab_role7.htm \
  dir_browser.htm \
  dir_browser2.htm \
  dir_browser3.htm \
  dir_browser4.htm \
  helpmenu.htm \
  ix.htm \
  ldapurl.htm \
  new_instance.htm \
  pixel.gif \
  property_editor.htm \
  property_editor2.htm \
  property_editor3.htm \
  property_editor4.htm \
  redir_agtoc.htm \
  redir_dochome.htm \
  replication_wizard.htm \
  replication_wizard2.htm \
  replication_wizard3.htm \
  replication_wizard4.htm \
  replication_wizard5.htm \
  replication_wizard6.htm \
  statustab_general.htm \
  statustab_logs.htm \
  statustab_logs2.htm \
  statustab_logs3.htm \
  statustab_performance.htm \
  statustab_performance2.htm \
  statustab_replication.htm \
  synchronization_wizard1.htm \
  synchronization_wizard2.htm \
  synchronization_wizard3.htm \
  taskstab_bkup_restore.htm \
  taskstab_bkup_restore2.htm \
  topics.htm \
  sniffer.js


BINS=$(addprefix $(HTMLDEST)/,$(HTML))

NEWBINS = $(addprefix $(NEWHTMLDEST)/,$(NEWHTML)) \
	$(addprefix $(HTMLDEST)/,$(NEWHTMLTOKEN)) \
	$(addprefix $(HTMLDEST)/,$(NEWHTMLTOPIC))

COPYRIGHTBINS = $(addprefix $(COPYRIGHTDEST)/,$(COPYRIGHTFILES))

all: $(HTMLDEST) $(BINS) $(NEWHTMLDEST) $(NEWBINS)

$(HTMLDEST):
	mkdir -p $(HTMLDEST)

$(NEWHTMLDEST):
	mkdir -p $(NEWHTMLDEST)

$(COPYRIGHTDEST):
	mkdir -p $(COPYRIGHTDEST)

strip:

depend:

clean:

	-$(RM) -f $(BINS) $(NEWBINS) 

$(HTMLDEST)/%.js: %.js
	-$(RM) -f $@
	cp $< $@

$(HTMLDEST)/%.map: %.map
	-$(RM) -f $@
	cp $< $@

$(HTMLDEST)/%.htm: %.htm
	-$(RM) -f $@
	cp $< $@

$(HTMLDEST)/%.jpg: %.jpg
	-$(RM) -f $@
	cp $< $@

$(NEWHTMLDEST)/%.htm: %.htm
	-$(RM) -f $@
	cp $< $@

$(HTMLDEST)/%.gif: %.gif
	-$(RM) -f $@
	cp $< $@

$(NEWHTMLDEST)/%.gif: %.gif
	-$(RM) -f $@
	cp $< $@

$(COPYRIGHTDEST)/%.html: copyright/%.html
	-$(RM) -f $@
	cp $< $@

HTMLDEFS=-DPRODUCT_NAME=$(PRODUCT) -D$(ARCH) -DARCH=$(PRETTY_ARCH)
