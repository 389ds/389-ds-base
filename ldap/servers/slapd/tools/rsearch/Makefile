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
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# gnu makefile for LDAP Server tools.
#

LDAP_SRC = ../../../..
BUILD_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/servers/tools/obj
BINDIR = $(RELDIR)/usr/bin
DATDIR = $(RELDIR)/usr/share/$(DS_BRAND)-ds/data

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), OSF1)
PLATFORM_SPECIFIC_EXTRA_LIBRARY = -lcxx
else # OSF1
# oems might need to edit this for their platform
PLATFORM_SPECIFIC_EXTRA_LIBRARY =
endif # OSF1

EXTRA_LIBS_DEP = $(LDAPSDK_DEP) $(DB_LIB_DEP) $(LDAP_COMMON_LIBS_DEP) $(SASL_DEP)

EXTRA_LIBS += $(LDAPLINK) $(DB_LIB) \
	$(PLATFORM_SPECIFIC_EXTRA_LIBRARY) \
	$(ALIBS) $(NSPRLINK) $(SECURITYLINK) \
	$(THREADSLIB) $(LDAP_COMMON_LIBS)  $(SASL_LINK)

ifeq ($(ARCH), Linux)
EXTRA_LIBS += -lcrypt
endif

# It looks like all of the latest versions of Unix that we ship on
# have a good enough heap implementations that they don't need
# SmartHeap.  We still need it on NT.
ifneq ($(ARCH), WINNT)
LDAP_DONT_USE_SMARTHEAP=1
endif

# Don't use smartheap for debug builds on NT
ifeq ($(ARCH), WINNT)
ifeq ($(DEBUG), full)
LDAP_DONT_USE_SMARTHEAP=1
endif
endif

ifndef LDAP_DONT_USE_SMARTHEAP
include $(BUILD_ROOT)/ns_usesh.mk
_smartheap_depend = $(SH_LIB_DEP)
else
CFLAGS+=-DLDAP_DONT_USE_SMARTHEAP
endif

RSEARCHSRC := nametable.c sdattable.c searchthread.c rsearch.c
	
INFADDSRC := nametable.c addthread.c infadd.c

DATAFILES := scripts/dbgen-GivenNames scripts/dbgen-FamilyNames scripts/dbgen-OrgUnits

DBGEN := scripts/dbgen.pl 

ifeq ($(OS_ARCH), WINNT)
OBJEXT :=.obj
else
OBJEXT :=.o
endif

RSEARCHOBJS	= $(addprefix $(OBJDEST)/, $(RSEARCHSRC:.c=$(OBJEXT)))
INFADDOBJS	= $(addprefix $(OBJDEST)/, $(INFADDSRC:.c=$(OBJEXT)))
HDIR		= $(LDAP_SRC)/include

ALL_OBJS = $(RSEARCHOBJS) $(INFADDOBJS)

RSEARCH = $(addsuffix $(EXE_SUFFIX), $(addprefix $(BINDIR)/, rsearch))
INFADD = $(addsuffix $(EXE_SUFFIX), $(addprefix $(BINDIR)/, infadd))
DBGEN = $(addsuffix $(EXE_SUFFIX), $(addprefix $(BINDIR)/, dbgen.pl))

BINS=	$(RSEARCH) $(INFADD) $(DBGEN)

all:	$(OBJDEST) $(BINDIR) $(BINS) $(DATDIR)

$(RSEARCH):	$(RSEARCHOBJS) $(BINDIR)
	$(LINK_EXE) $(RSEARCHOBJS) $(LDAP_LIBLDIF)
	-chmod 755 $(RSEARCH)

$(INFADD):	$(INFADDOBJS) $(BINDIR)
	$(LINK_EXE) $(INFADDOBJS) $(LDAP_LIBLDIF)
	-chmod 755 $(INFADD)

$(DBGEN): scripts/dbgen.pl $(BINDIR)
	cp scripts/dbgen.pl $(BINDIR)
	-chmod 755 $(DBGEN)

$(OBJDEST):
	if [ ! -d $(OBJDEST) ]; then \
	  $(MKDIR) $(OBJDEST); \
	fi

$(BINDIR):
	if [ ! -d $(BINDIR) ]; then \
	  $(MKDIR) $(BINDIR); \
	fi

$(DATDIR):
	-$(RM) -r $(DATDIR)
	-$(MKDIR) $(DATDIR)
	cp scripts/dbgen-* $(DATDIR)

clean:
	-$(RM) $(ALL_OBJS)
	-$(RM) -r $(BINS) $(DATDIR)

