#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

MCOM_ROOT = ../../../../..
LDAP_SRC = ../../..

OBJDEST = $(OBJDIR)/servers/obj
BINDIR = $(LDAP_SERVER_RELDIR)
ALLDIRS = $(BINDIR) $(OBJDEST)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

LINK32=link.exe

MC=mc	#message compiler?

DLLS = nsldapctr

# -D's get all screwed up: we need to fix them:
CFLAGS+= -UNS_PERSONAL -DNS_DS
# this lets us pickup regparms.h
CFLAGS += -I $(MCOM_ROOT)/ldapserver/include

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
