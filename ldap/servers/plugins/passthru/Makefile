#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server "Pass Through Authentication" plugin
#
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libpassthru
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libpassthru.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

PASSTHRU_OBJS=	ptbind.o ptconfig.o ptconn.o ptdebug.o ptpreop.o ptutil.o

OBJS = $(addprefix $(OBJDEST)/, $(PASSTHRU_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBPASSTHRU_DLL_OBJ = $(addprefix $(OBJDEST)/, ptdllmain.o)
endif

LIBPASSTHRU=	$(addprefix $(LIBDIR)/, $(PASSTHRU_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(NSPRLINK)
endif


ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libpassthru.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPDLINK) $(LDAP_SDK_LIBLDAP_DLL) $(NSPRLINK)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBPASSTHRU)

$(LIBPASSTHRU): $(OBJS) $(LIBPASSTHRU_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBPASSTHRU_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBPASSTHRU_DLL_OBJ)
endif
	$(RM) $(LIBPASSTHRU)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS):	passthru.h
