#
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
# 	Make file SNMP subagent for Fedora Directory Server
#
#
# 	Revision History:
#	
#	07/31/97 Created by stevross
#	
#  

BUILD_ROOT = ../../..
LDAP_SRC = ../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/ldap-agent
BINDIR = $(LDAP_SERVER_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ARCH := $(shell uname -s)
ifneq ($(ARCH), WINNT)
ARCH := $(shell $(BUILD_ROOT)/nsarch)
endif

ifeq ($(USE_64), 1)
 ifeq ($(ARCH), HPUX)
  ifeq ($(NSOS_RELEASE), B.11.23)
    LDFLAGS += -lpthread +DD64 +Z
  else
    LDFLAGS += -lpthread +DA2.0W +DS2.0 +Z
  endif
 endif
 ifeq ($(ARCH), SOLARIS)
  LDFLAGS += -xarch=v9
 endif
endif

SNMP_OBJS = main.o ldap-agent.o agtmmap.o
OBJS = $(addprefix $(OBJDEST)/, $(SNMP_OBJS))
SNMPMODULE  = ldap-agent
AGTMMAP_DIR = $(LDAP_SRC)/servers/slapd

ifneq ($(ARCH), WINNT)
  INCLUDES += -I. $(NETSNMP_INCLUDE) $(NSPR_INCLUDE)
  EXTERNAL_AGENT_LIBS = $(shell $(NETSNMP_BINDIR)/net-snmp-config --external-agent-libs)
  EXTRA_LIBS += $(NETSNMP_LINK) $(EXTERNAL_AGENT_LIBS)
endif

# the redhat-directory.mib goes in the _datadir/brand-ds/plugins/snmp directory, and the other mib like
# files go in the _datadir/brand-ds/plugins/snmp/mibs directory
MIB_DEST_DIR = $(RELDIR)/$(DS_PLUGINDIR)/snmp
MIB_SRC_FILES =redhat-directory.mib
MIBS_DEST_DIR = $(MIB_DEST_DIR)/mibs
MIBS_SRC_FILES = NETWORK-SERVICES-MIB.txt \
	RFC1155-SMI.txt	\
	RFC-1215.txt \
	SNMPv2-CONF.txt \
	SNMPv2-SMI.txt \
	SNMPv2-TC.txt
MIB_DEST_FILES = $(addprefix $(MIBS_DEST_DIR)/,$(notdir $(MIBS_SRC_FILES))) \
	$(addprefix $(MIB_DEST_DIR)/,$(MIB_SRC_FILES))

default: all

ifneq ($(ARCH), WINNT)
all: $(OBJDEST) $(BINDIR)/$(SNMPMODULE) $(MIB_DEST_DIR)/$(MIB_SRC_FILES) $(MIB_DEST_FILES)
else
OBJ_SUFFIX=obj
all: $(MIB_DEST_FILES)
	cd ntagt; $(MAKE) $(MFLAGS) all
endif

# Rule to make agtmmap
$(OBJDEST)/agtmmap.o: $(AGTMMAP_DIR)/agtmmap.c
	$(CC) $(CFLAGS) -g -o $@ -c $<

# Rule to create destination directories
$(MIBS_DEST_DIR) $(MIB_DEST_DIR) $(OBJDEST):
	$(MKDIR) $@

# Rule to build subagent binary
$(BINDIR)/$(SNMPMODULE): $(OBJS)
	$(LINK_EXE)

# this rule is for mib files in the local directory that go in the nsmib directory
$(MIB_DEST_DIR)/$(MIB_SRC_FILES): $(MIB_DEST_DIR)
	$(CP) ./$(MIB_SRC_FILES) $@

# this rule is for mib files which go in the mibs subdir
$(MIBS_DEST_DIR)/%: % $(MIBS_DEST_DIR)
	$(CP) $< $@

clean: localclean
 
localclean:
ifneq ($(ARCH), WINNT)
	$(RM) $(EXTDEST)/$(SNMPMODULE)$(EXE_SUFFIX)
endif

