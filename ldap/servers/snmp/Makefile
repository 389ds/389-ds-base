#
# BEGIN COPYRIGHT BLOCK
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

SNMP_OBJS = main.o ldap-agent.o agtmmap.o
OBJS = $(addprefix $(OBJDEST)/, $(SNMP_OBJS))
SNMPMODULE  = ldap-agent
AGTMMAP_DIR = $(LDAP_SRC)/servers/slapd

ifneq ($(ARCH), WINNT)
  INCLUDES += -I. $(NETSNMP_INCLUDE) $(NSPR_INCLUDE)
  EXTERNAL_AGENT_LIBS = $(shell $(NETSNMP_BINDIR)/net-snmp-config --external-agent-libs)
  EXTRA_LIBS += $(NETSNMP_LINK) $(EXTERNAL_AGENT_LIBS)
endif

# the redhat-directory.mib goes in the plugins/snmp directory, and the other mib like
# files go in the plugins/snmp/mibs directory
MIB_DEST_DIR = $(RELDIR)/plugins/snmp
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

