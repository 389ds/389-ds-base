#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server NT messages DLL
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/ntmsgdll/obj
BINDIR = $(LDAP_SERVER_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

MSGFILE = slapdmessages30

MSG_SRC = ntslapdmessages

NTMSGDLL = $(addprefix $(BINDIR)/, $(MSGFILE).$(DLL_SUFFIX))

LDAP_MSG_OBJS=	$(MSG_SRC).o $(MSG_SRC).res

OBJS = $(addprefix $(OBJDEST)/, $(LDAP_MSG_OBJS)) 

clientSDK: all

all:	$(OBJDEST) $(BINDIR) $(NTMSGDLL)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(OBJDEST)/msg00001.bin
	$(RM) $(LDAP_HDIR)/$(MSG_SRC).h
	$(RM) $(OBJDEST)/$(MSG_SRC).rc
	$(RM) $(NTMSGDLL)

$(OBJDEST):
	$(MKDIR) $@

$(NTMSGDLL): $(OBJS) $(LDAP_HDIR)/$(MSG_SRC).h
	$(LINK_DLL)

$(LDAP_HDIR)/$(MSG_SRC).h: $(MSG_SRC).mc

$(OBJDEST)/msg0001.bin: $(MSG_SRC).mc

$(OBJDEST)/$(MSG_SRC).rc: $(MSG_SRC).mc 
	$(MC) $< -h $(LDAP_HDIR) -r $(OBJDEST)

$(OBJDEST)/$(MSG_SRC).res: $(OBJDEST)/$(MSG_SRC).rc
	$(RSC) -fo $(OBJDEST)/$(MSG_SRC).res $< 
