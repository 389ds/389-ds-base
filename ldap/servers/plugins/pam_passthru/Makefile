#
# BEGIN COPYRIGHT BLOCK
# Copyright 2005 Red Hat
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server "PAM Pass Through Authentication" plugin
#
#

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libpam_passthru
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libpam_passthru.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

PAM_PASSTHRU_OBJS=	pam_ptimpl.o pam_ptconfig.o pam_ptdebug.o pam_ptpreop.o

OBJS = $(addprefix $(OBJDEST)/, $(PAM_PASSTHRU_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBPAM_PASSTHRU_DLL_OBJ = $(addprefix $(OBJDEST)/, pam_ptdllmain.o)
endif

LIBPAM_PASSTHRU=	$(addprefix $(LIBDIR)/, $(PAM_PASSTHRU_DLL).$(DLL_SUFFIX))

EXTRA_LIBS += -lpam
#LD += -Xlinker --no-undefined -Xlinker --no-allow-shlib-undefined
#LD += -Xlinker --export-dynamic

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(NSPRLINK)
endif


ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libpam_passthru.def"
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

all:	$(OBJDEST) $(LIBDIR) $(LIBPAM_PASSTHRU)

$(LIBPAM_PASSTHRU): $(OBJS) $(LIBPAM_PASSTHRU_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBPAM_PASSTHRU_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBPAM_PASSTHRU_DLL_OBJ)
endif
	$(RM) $(LIBPAM_PASSTHRU)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS):	pam_passthru.h
