#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server syntax-plugin.so syntax plugins
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libsyntax
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libsyntax.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

SYNTAX_OBJS=	phonetic.o string.o cis.o sicis.o ces.o bin.o tel.o dn.o int.o \
		value.o debug.o

OBJS = $(addprefix $(OBJDEST)/, $(SYNTAX_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBSYNTAX_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LIBSYNTAX=	$(addprefix $(LIBDIR)/, $(SYNTAX_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
endif
ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libsyntax.def"
CFLAGS+= /WX 
endif # WINNT

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBSYNTAX)

$(LIBSYNTAX): $(OBJS) $(LIBSYNTAX_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBSYNTAX_DLL_OBJ) $(EXTRA_LIBS) 

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBSYNTAX_DLL_OBJ)
endif
	$(RM) $(LIBSYNTAX)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
