#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for LDAP Back-ldif backend
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libback-ldif
LIBDIR = $(LDAP_LIBDIR)
SERVER_OBJDEST = $(OBJDIR)/servers/obj

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

BACKLDIF_OBJS	= close.o delete.o modrdn.o unbind.o add.o \
	compare.o init.o search.o bind.o config.o modify.o monitor.o \
	start.o

OBJS = $(addprefix $(OBJDEST)/, $(BACKLDIF_OBJS)) 

SERVER_OBJS= ch_malloc.o entry.o result.o modutil.o

EXTRA_OBJS = $(addprefix $(SERVER_OBJDEST)/, $(SERVER_OBJS))

INCLUDES += -I..

ifeq ($(ARCH), WINNT)
BACKLDIF_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LDAP_BACKLDIF=	$(addprefix $(LIBDIR)/, $(LIBBACK_LDIF_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSECURITY) $(LIBNSPR) \
	$(LDAP_COMMON_LIBS_DEP) $(LDAP_SDK_LIBS_DEP) \
	$(LIBSLAPD_DEP) $(LIBLDAPU_DEP)

EXTRA_LIBS += $(LIBSECURITY) $(LIBNSPR) \
	$(LDAP_COMMON_LIBS) $(LDAP_SDK_LIBS) \
	$(LIBSLAPD) $(THREADSLIB) $(LIBLDAPU)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS)
endif

clientSDK:

all:	$(OBJDEST) $(LIBDIR) $(LDAP_BACKLDIF)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)

$(LDAP_BACKLDIF): $(OBJS) $(BACKLDIF_DLL_OBJ)
	$(LINK_DLL) $(BACKLDIF_DLL_OBJ) $(EXTRA_LIBS)

$(SERVER_OBJDEST)/ch_malloc.o: ../ch_malloc.c
	$(CC) -c $(CFLAGS) $(MCC_INCLUDE) $< $(OFFLAG)$*.o

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(BACKLDIF_DLL_OBJ)
endif
	$(RM) $(LDAP_BACKLDIF)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

