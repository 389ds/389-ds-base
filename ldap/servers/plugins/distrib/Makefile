#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright 2001 Netscape Communications Corp.
# All rights reserved.
# 
#
# GNU Makefile for Directory Server distribution plugin
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libdistrib
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libdistrib.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

DIS_OBJS= \
	distrib.o


OBJS = $(addprefix $(OBJDEST)/, $(DIS_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBDIS_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

# The sample distribution plugin is not part of DS.
# So we generate the shared library outside of $(LIBDIR)
# so that it's not retreived by the packaging makefiles.
#LIBDIS =	$(addprefix $(LIBDIR)/, $(DIS_DLL).$(DLL_SUFFIX))
LIBDIS =	$(addprefix $(OBJDEST)/, $(DIS_DLL).$(DLL_SUFFIX))

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
	$(LDAP_COMMON_LIBS)
endif
ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += \
	$(LIBSLAPD_DEP) \
	$(LDAP_LIBUTIL_DEP) \
	$(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += \
	$(LDAPSDK_DEP)
EXTRA_LIBS += \
	$(LIBSLAPDLINK) \
	$(LDAP_SDK_LIBLDAP_DLL) \
	$(LIBUTIL) \
	$(NSPRLINK) \
	$(LDAP_COMMON_LIBS)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libdistrib.def"
CFLAGS+= /WX 
endif # WINNT

ifeq ($(ARCH), AIX)
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBDIS)

$(LIBDIS): $(OBJS) $(LIBDIS_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBDIS_DLL_OBJ) $(EXTRA_LIBS) 

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBDIS_DLL_OBJ)
endif
	$(RM) $(LIBDIS)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
