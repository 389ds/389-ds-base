#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server acl-plugin.so acl plugins
#

LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libacl
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

# ACL plugin depends on libadminutil
MCC_INCLUDE += $(ADMINUTIL_INCLUDE)

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libacl.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd -I$(ACLINC)

ACL_OBJS=	acl.o acllas.o aclutil.o aclplugin.o aclparse.o acl_ext.o aclproxy.o \
			aclinit.o aclgroup.o aclanom.o acllist.o acleffectiverights.o

OBJS = $(addprefix $(OBJDEST)/, $(ACL_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBACL_DLL_OBJ = $(addprefix $(OBJDEST)/, acldllmain.o)
endif

LIBACL=	$(addprefix $(LIBDIR)/, $(ACL_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(NSPRLINK) $(LDAP_LIBAVL) $(LDAP_SDK_LIBLDAP_DLL)
endif

# ACL plugin depends on libadminutil (through libns-httpd)
EXTRA_LIBS_DEP += $(NSHTTPD_DEP) $(ADMINUTIL_DEP) $(DBM_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(DBMLINK)

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBACCESS_DEP)
EXTRA_LIBS += $(LIBACCESS)
endif

ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libacl.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPDLINK) $(NSPRLINK) $(LDAP_LIBAVL) $(LDAP_SDK_LIBLDAP_DLL)
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBACL)

$(LIBACL): $(OBJS) $(LIBACL_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBACL_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBACL_DLL_OBJ)
endif
	$(RM) $(LIBACL)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
