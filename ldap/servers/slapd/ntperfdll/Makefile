#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

BUILD_ROOT = ../../../..
LDAP_SRC = ../../..

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(LDAP_SERVER_RELDIR)
ALLDIRS = $(BINDIR) $(OBJDEST)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

LINK32=link.exe

MC=mc	#message compiler?

DLLS = nsldapctr

# -D's get all screwed up: we need to fix them:
CFLAGS+= -UNS_PERSONAL -DNS_DS
# this lets us pickup regparms.h
CFLAGS += -I $(BUILD_ROOT)/include

MCS = nsldapctrmc
DEPMC = $(addsuffix .h, $(MCS))

OBJS  = $(OBJDEST)/nsldapctr.o $(OBJDEST)/nsldapctrutil.o

RELFILES = nsldapctrs.ini nsldapctrdef.h

DESTRELFILES = $(addprefix $(LDAP_INSTALL_BIN_RELDIR)/, $(RELFILES))

all: $(ALLDIRS) $(addprefix $(BINDIR)/, $(addsuffix .dll, $(DLLS) ) ) $(DESTRELFILES)

nsldapctrmc.h: 
	$(MC) -s -v $*.mc

$(OBJDEST)/%.o: %.cpp
	cl -nologo -c $(CFLAGS) $(MCC_INCLUDE) /Tp $< -Fo$@

$(BINDIR)/nsldapctr.dll: $(DEPMC) $(OBJS)
	$(LINK_DLL) /OUT:$@ /DEF:exports.def

$(LDAP_INSTALL_BIN_RELDIR)/%: % $(LDAP_INSTALL_BIN_RELDIR)
	$(CP) $< $(dir $@)
