#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
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

OBJDEST = $(OBJDIR)/lib/libuidunique
LIBDIR = $(LIB_RELDIR)
SHAREDLIB = $(OBJDIR)/lib/shared/utils.o

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libuiduniq.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd -I../shared

LOCAL_OBJS=  uid.o 7bit.o

SHAREDDIR= ../shared

OBJS = $(addprefix $(OBJDEST)/, $(LOCAL_OBJS)) 

ifeq ($(ARCH), WINNT)
#LIBUIDUNIQUE_DLL_OBJ = $(addprefix $(OBJDEST)/, uid.o 7bit.o)
endif

LIBUIDUNIQUE=	$(addprefix $(LIBDIR)/, $(UID_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
endif



ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libuiduniq.def"
EXTRA_LIBS += $(NSPRLINK)
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
EXTRA_LIBS += $(DLL_EXTRA_LIBS)
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

EXTRA_LIBS += $(SHAREDLIB)

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBUIDUNIQUE)

$(LIBUIDUNIQUE): $(OBJS) $(LIBUIDUNIQUE_DLL_OBJ) $(DEF_FILE)
#	$(LINK_DLL) $(LIBUIDUNIQUE_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)
	$(LINK_DLL) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBUIDUNIQUE_DLL_OBJ)
endif
	$(RM) $(LIBUIDUNIQUE)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS): $(LDAP_SRC)/servers/slapd/slapi-plugin.h \
          ../shared/plugin-utils.h 

