#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server "Retrocl" plugin

LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/retrocl-plugin
BINDIR = $(LDAP_SERVER_RELDIR)
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(BUILD_ROOT)/ns_usedb.mk
INCLUDES+=-I$(DB_INCLUDE)

ifeq ($(ARCH), WINNT)
DEF_FILE:=./retrocl.def
endif

CFLAGS += $(SLCFLAGS) -DSLAPD_LOGGING

ifeq ($(ARCH), WINNT)
CFLAGS += /WX
endif

ifdef TEST_CL5
CFLAGS += -DTEST_CL5
endif

INCLUDES += -I$(LDAP_SRC)/servers/slapd -I$(DB_INCLUDE)

ifeq ($(ARCH), WINNT)
SUBSYSTEM=console
endif

LOCAL_OBJS= \
	retrocl.o \
	retrocl_po.o \
	retrocl_rootdse.o \
	retrocl_cn.o \
	retrocl_trim.o \
	retrocl_create.o \



LIBRETROCL_OBJS = $(addprefix $(OBJDEST)/, $(LOCAL_OBJS)) 

ifeq ($(ARCH), WINNT)
RETROCL_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

LIBRETROCL=	$(addprefix $(LIBDIR)/, $(RETROCL_DLL).$(DLL_SUFFIX))

LT_OBJS = $(addprefix $(OBJDEST)/, linktest.o) 

#EXTRA_LIBS_DEP = $(LDAPSDK_DEP) \
#	$(LDAP_LIBLDIF_DEP) \
#	$(LDAP_SLIBLCACHE_DEP) $(DB_LIB_DEP) $(LIBSLAPD_DEP) \
#	$(LDAP_COMMON_LIBS_DEP)

#EXTRA_LIBS = $(LIBACCESS) $(LDAP_SDK_LIBSSLDAP_LIB) $(ADMINUTIL_LINK) \
#	$(LDAP_SDK_LIBLDAP_DLL) $(LDAP_SLIBLCACHE) $(DB_LIB) \
#	$(PLATFORM_SPECIFIC_EXTRA_LIBRARY) $(LIBSLAPD)  $(LDAP_LIBLITEKEY) \
#	$(NLSLINK) $(ALIBS) \
#	$(LDAP_SDK_LIBSSLDAP_LIB) $(LDAP_SDK_LIBLDAP_DLL) \
#	$(LIBSECURITYLINK) $(NSPRLINK) $(DBMLINK) \
#	$(THREADSLIB) $(LDAP_COMMON_LIBS) $(NSPRLINK) $(SVRCORELINK)

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(DB_LIB_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(DB_LIB)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(DB_LIB_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(DB_LIB)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./retrocl.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBRETROCL) 

linktest: $(LIBRETROCL) $(LT_OBJS)
	$(LINK_EXE_NOLIBSOBJS) -o linktest $(LT_OBJS) $(LIBRETROCL) -Rlib -Rlib/../bin/slapd/lib -Llib -Llib/../bin/slapd/lib  -lslapd $(EXTRA_LIBS) $(NSPRLINK)


$(LIBRETROCL): $(LIBRETROCL_OBJS) $(RETROCL_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBRETROCL_OBJS) $(RETROCL_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS) $(LDAP_LIBLDIF) $(NSPRLINK)

tests:	$(TEST_PROGS)

veryclean: clean

clean:
	$(RM) $(LIBRETROCL_OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(RETROCL_DLL_OBJ)
endif
	$(RM) $(LIBRETROCL)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(LIBRETROCL_OBJS):	
