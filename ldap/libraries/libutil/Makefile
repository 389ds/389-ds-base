#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for libutil
#

LDAP_SRC = ../..
MCOM_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libutil
LIBDIR = $(LDAP_LIBDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

#
# ntdebug.c currently not used
#
LIBUTIL_OBJS=	getopt.o ntevent.o \
	ntreg.o ntstubs.o

ifeq ($(ARCH), WINNT)
LIBUTIL_OBJS += crypt.o
endif

OBJS = $(addprefix $(OBJDEST)/, $(LIBUTIL_OBJS)) 

LIBUTIL=	$(addprefix $(LIBDIR)/, libutil.$(LIB_SUFFIX))

INCLUDES += -I$(LDAP_SRC)/servers/slapd -I$(OBJDIR)/include

SLAPDMESSAGES_H=$(MCOM_ROOT)/ldapserver/ldap/include/ntslapdmessages.h

ifeq ($(LDAP_NO_LIBLCACHE),1)
CFLAGS+=-DNO_LIBLCACHE
endif

clientSDK: all

all:	$(OBJDEST) $(SLAPDMESSAGES_H) $(LIBDIR) $(OBJS) $(LIBUTIL)

$(LIBDIR):
		$(MKDIR) $(LIBDIR)

$(LIBUTIL): $(OBJS)
	$(LINK_LIB)

$(SLAPDMESSAGES_H):
	@echo target: $@
	cd $(MCOM_ROOT)/ldapserver/ldap/servers/slapd/ntmsgdll; $(MAKE) $(MFLAGS)

veryclean: clean

clean:
	$(RM) $(OBJS)
	$(RM) $(LIBUTIL)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

