#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# 	Make file SNMP subagent for Brandx Directory Server
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

OBJDEST = $(OBJDIR)/ns-ldapagt/obj
INCLDEST = $(OBJDIR)/ns-ldapagt/include
BINDIR = $(LDAP_SERVER_RELDIR)
EXTDEST = $(BINDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

MCC_SERVER=

ARCH := $(shell uname -s)
ifneq ($(ARCH), WINNT)
ARCH := $(shell $(BUILD_ROOT)/nsarch)
endif


SNMPMODULE  =   ns-ldapagt

# the netscape-ldap.mib goes in the plugins/snmp directory, and the other mib like
# files go in the plugins/snmp/mibs directory
NSMIB_DEST_DIR = $(RELDIR)/plugins/snmp
NSMIB_SRC_FILES = netscape-ldap.mib
MIBS_DEST_DIR = $(NSMIB_DEST_DIR)/mibs
MIBS_SRC_FILES = NETWORK-SERVICES-MIB.txt \
	RFC1155-SMI.txt	\
	RFC-1215.txt \
	SNMPv2-CONF.txt \
	SNMPv2-SMI.txt \
	SNMPv2-TC.txt
MIB_DEST_FILES = $(addprefix $(MIBS_DEST_DIR)/,$(notdir $(MIBS_SRC_FILES))) \
	$(addprefix $(NSMIB_DEST_DIR)/,$(NSMIB_SRC_FILES))

default: all

ifneq ($(ARCH), WINNT)
all: $(EXTDEST)/$(SNMPMODULE) $(NSMIB_DEST_DIR)/$(NSMIB_SRC_FILES) $(MIB_DEST_FILES)
else
OBJ_SUFFIX=obj
all: $(MIB_DEST_FILES)
	cd ntagt; $(MAKE) $(MFLAGS) all
endif

# Rule to create destination directories
$(MIBS_DEST_DIR) $(NSMIB_DEST_DIR):
	$(MKDIR) $@

# Rule to copy subagent binary to release area
$(EXTDEST)/$(SNMPMODULE): $(EXTDEST)
ifneq ($(ARCH), WINNT)
	$(CP) $(NSCP_DISTDIR)/peer/obj/$(SNMPMODULE) $(EXTDEST)/$(SNMPMODULE)
endif

# this rule is for mib files in the local directory that go in the nsmib directory
$(NSMIB_DEST_DIR)/$(NSMIB_SRC_FILES): $(NSMIB_DEST_DIR)
	$(CP) ./$(NSMIB_SRC_FILES) $@

# this rule is for mib files which go in the mibs subdir
$(MIBS_DEST_DIR)/%: % $(MIBS_DEST_DIR)
	$(CP) $< $@

clean: localclean
 
localclean:
ifneq ($(ARCH), WINNT)
	$(RM) $(EXTDEST)/$(SNMPMODULE)$(EXE_SUFFIX)
endif

