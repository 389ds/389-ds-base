#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server password_storaged-plugin.so password storage scheme plugins
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libdes
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(BUILD_ROOT)/ns_usepurify.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libdes.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd
INCLUDES += -I$(BUILD_ROOT)/ldap/include

REVER_OBJS= \
	rever.o des.o

OBJS = $(addprefix $(OBJDEST)/, $(REVER_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBREVER_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

REVER_DLL = des-plugin
LIBREVER =	$(addprefix $(LIBDIR)/, $(REVER_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += \
	$(LIBSLAPD_DEP) \
	$(LDAP_LIBUTIL_DEP) \
	$(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP) \
	$(SECURITY_DEP)
EXTRA_LIBS += \
	$(LIBSLAPD) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LIBUTIL) \
	$(NSPRLINK) \
	$(LDAP_COMMON_LIBS) \
	$(SECURITYLINK)
endif
ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += \
	$(LIBSLAPD_DEP) \
	$(LDAP_LIBUTIL_DEP) \
	$(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP) \
	$(SECURITY_DEP)
EXTRA_LIBS += \
	$(LIBSLAPDLINK) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LIBUTIL) \
	$(NSPRLINK) \
	$(LDAP_COMMON_LIBS) \
	$(SECURITYLINK)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libdes.def"
CFLAGS+= /WX 
endif # WINNT

ifeq ($(ARCH), AIX)
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBREVER)

$(LIBREVER): $(OBJS) $(LIBREVER_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBREVER_DLL_OBJ) $(EXTRA_LIBS) 

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBREVER_DLL_OBJ)
endif
	$(RM) $(LIBREVER)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
