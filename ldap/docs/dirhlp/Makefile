#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms.
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2002-2003 Netscape Communications Corporation.
# All rights reserved.
#
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
