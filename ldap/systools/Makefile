#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# gnu makefile for LDAP Server tools.
#

LDAP_SRC = ..
BUILD_ROOT = ../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/servers/tools/obj
BINDIR = $(LDAP_SERVER_RELDIR)

SLAPD_OBJDIR = $(LDAP_OBJDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), OSF1)
PLATFORM_SPECIFIC_EXTRA_LIBRARY = -lcxx
else # OSF1
# oems might need to edit this for their platform
PLATFORM_SPECIFIC_EXTRA_LIBRARY =
endif # OSF1


ifeq ($(USE_64), 1)
ifeq ($(ARCH), HPUX)
LDFLAGS += +DA2.0W +DS2.0 +Z
endif
ifeq ($(ARCH), SOLARIS)
LDFLAGS += -xarch=v9
endif
endif

LDFLAGS	+= $(EXLDFLAGS)

DEPLIBS=

EXTRA_LIBS_DEP =

EXTRA_LIBS = $(PLATFORM_SPECIFIC_EXTRA_LIBRARY)

ifeq ($(ARCH), Linux)
EXTRA_LIBS += -lcrypt
endif

ifeq ($(ARCH), SOLARIS)
EXTRA_LIBS += -lelf
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS += -lodm
endif

TOOL_OBJS =  idsktune.o pio.o viewcore.o
ALL_OBJS = $(addprefix $(OBJDEST)/, $(TOOL_OBJS))

DSKTUNE = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, dsktune))
VIEWCORE = $(addsuffix $(EXE_SUFFIX), \
	$(addprefix $(BINDIR)/, viewcore))

ifeq ($(ARCH), SOLARIS)
BINS=	$(DSKTUNE) $(VIEWCORE)
else
BINS=	$(DSKTUNE) 
endif

all:	$(OBJDEST) $(BINDIR) $(BINS)

$(DSKTUNE):	$(OBJDEST)/idsktune.o $(OBJDEST)/pio.o $(EXTRA_LIBS_DEP)
	$(LINK_EXE_NOLIBSOBJS) $< $(OBJDEST)/pio.o $(EXTRA_LIBS)

$(VIEWCORE):	$(OBJDEST)/viewcore.o $(EXTRA_LIBS_DEP)
	$(LINK_EXE_NOLIBSOBJS) $< $(EXTRA_LIBS) 

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(OBJDEST)/idsktune.o: sol_patches.c hp_patches.c

clean:
	-$(RM) $(ALL_OBJS)
	-$(RM) $(BINS)


