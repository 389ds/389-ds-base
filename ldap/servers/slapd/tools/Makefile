#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# gnu makefile for LDAP Server tools.
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/servers/tools/obj
BINDIR = $(LDAP_SERVER_RELDIR)

SLAPD_OBJDIR = $(LDAP_OBJDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
ifndef LDAP_USE_OLD_DB
include $(BUILD_ROOT)/ns_usedb.mk
INCLUDES+=-I$(DB_INCLUDE)
else
CFLAGS+=-DLDAP_USE_DB185
endif

SLAPDHDIR = ../

ifeq ($(ARCH), OSF1)
PLATFORM_SPECIFIC_EXTRA_LIBRARY = -lcxx
else # OSF1
# oems might need to edit this for their platform
PLATFORM_SPECIFIC_EXTRA_LIBRARY =
endif # OSF1

INCLUDES += $(SSLINCLUDE)
DEFS += $(SSL)

OBJS1 += $(OBJDEST)/pwenc.o 

ifeq ($(USE_64), 1)
ifeq ($(ARCH), HPUX)
LDFLAGS += -lpthread +DA2.0W +DS2.0 +Z
endif
ifeq ($(ARCH), SOLARIS)
LDFLAGS += -xarch=v9 
endif
endif


INCLUDES += -I$(SLAPDHDIR) -I$(LDAP_ADMINCDIR)
INCLUDES += -I$(ACLINC)
INCLUDES +=  -I ../../plugins/rever
LDFLAGS	+= $(EXLDFLAGS) $(SSLLIBFLAG)

ifeq ($(ARCH), WINNT)
SUBSYSTEM=console
endif

DEPLIBS=

EXTRA_LIBS_DEP = $(LDAPSDK_DEP) \
	$(LDAP_LIBLDIF_DEP) \
	$(LDAP_SLIBLCACHE_DEP) $(DB_LIB_DEP) $(LIBSLAPD_DEP) \
	$(LDAP_COMMON_LIBS_DEP)

EXTRA_LIBS = $(LDAPLINK) \
	$(LDAP_SLIBLCACHE) $(DB_LIB) \
	$(PLATFORM_SPECIFIC_EXTRA_LIBRARY) $(LIBSLAPD) $(LDAP_LIBLITEKEY) \
	$(ALIBS) \
	$(SECURITYLINK) $(DBMLINK) \
	$(THREADSLIB) $(LDAP_COMMON_LIBS) $(NSPRLINK) $(SVRCORELINK)

ifeq ($(ARCH), Linux)
EXTRA_LIBS += -lcrypt
endif


KEYUPG_LIBS_DEP=
KEYUPG_LIBS=$(LDAP_LIBLITEKEY)

ifeq ($(ARCH), WINNT)
KEYUPG_LIBS_DEP=$(LDAP_LIBUTIL_DEP)
KEYUPG_LIBS += $(LDAP_LIBUTIL)
endif

ifdef HEAPAGENT
CFLAGS+=-DPURIFYING
LDAP_DONT_USE_SMARTHEAP=1
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


TOOL_OBJS =  ldif.o keyupg.o pwenc.o mmldif.o migratecred.o eggencode.o
ALL_OBJS = $(addprefix $(OBJDEST)/, $(TOOL_OBJS))

LDIF = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, ldif))
PWDHASH = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, pwdhash))
MIGRATECRED = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, migratecred))
KEYUPG = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, keyupg))
MMLDIF = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, mmldif))
EGGENCODE = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, eggencode))

BINS=	$(LDIF) $(PWDHASH) $(KEYUPG) $(MMLDIF) $(MIGRATECRED)
EXTRABINS=	$(EGGENCODE)

all:	$(OBJDEST) $(BINDIR) $(LDAP_ADMIN_BIN_RELDIR) $(BINS)

extras:	$(OBJDEST) $(BINDIR) $(EGGENCODE)

$(LDIF):	$(OBJDEST)/ldif.o $(LDAP_LIBLDIF_DEP)
	$(LINK_EXE) $< $(LDAP_LIBLDIF)

$(PWDHASH):	$(OBJS1) $(EXTRA_LIBS_DEP)
	$(LINK_EXE) $(OBJS1) $(EXTRA_LIBS) 

$(MIGRATECRED):	$(OBJDEST)/migratecred.o $(EXTRA_LIBS_DEP)
	$(LINK_EXE) $(OBJDEST)/migratecred.o $(EXTRA_LIBS) 

$(KEYUPG):      $(OBJDEST)/keyupg.o  $(KEYUPG_LIBS_DEP)
	$(LINK_EXE_NOLIBSOBJS) $<  $(KEYUPG_LIBS)

$(MMLDIF):	$(OBJDEST)/mmldif.o $(EXTRA_LIBS_DEP)
	$(LINK_EXE_NOLIBSOBJS) $(OBJDEST)/mmldif.o $(EXTRA_LIBS) 

$(EGGENCODE):	$(OBJDEST)/eggencode.o
	$(LINK_EXE_NOLIBSOBJS) $(OBJDEST)/eggencode.o

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

clean:
	-$(RM) $(ALL_OBJS)
	-$(RM) $(BINS) $(EXTRABINS)

