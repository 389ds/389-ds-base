#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for Directory Server "Chaining Backend" plugin
#

LDAP_SRC = ../../..

MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libcb
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./libcb.def
endif

CFLAGS+=$(SLCFLAGS)

INCLUDES += -I$(LDAP_SRC)/servers/slapd

CB_OBJS= cb_temp.o cb_init.o cb_config.o cb_instance.o cb_start.o cb_search.o cb_utils.o cb_add.o cb_delete.o cb_schema.o \
cb_acl.o cb_modify.o cb_compare.o cb_modrdn.o cb_abandon.o cb_conn_stateless.o cb_bind.o cb_unbind.o cb_monitor.o \
cb_controls.o cb_size.o cb_test.o cb_close.o cb_cleanup.o cb_debug.o

OBJS = $(addprefix $(OBJDEST)/, $(CB_OBJS)) 

ifeq ($(ARCH), WINNT)
LIBCB_DLL_OBJ = $(addprefix $(OBJDEST)/, cbdllmain.o)
endif

LIBCB=	$(addprefix $(LIBDIR)/, $(CB_DLL).$(DLL_SUFFIX))

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP) $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(SECURITY_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(LDAP_LIBUTIL) $(LDAP_COMMON_LIBS) $(SECURITYLINK) $(NSPRLINK)

endif


ifeq ($(ARCH), WINNT)
DLL_LDFLAGS += -def:"./libcb.def"
endif # WINNT

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAP_LIBUTIL_DEP) $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(SECURITY_DEP) $(NSPR_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL) $(LIBUTIL)  $(LDAP_COMMON_LIBS)  $(SECURITYLINK) $(NSPRLINK) 
EXTRA_LIBS += $(DLL_EXTRA_LIBS) 
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(LIBCB)

$(LIBCB): $(OBJS) $(LIBCB_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(LIBCB_DLL_OBJ) $(PLATFORMLIBS) $(EXTRA_LIBS)

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(LIBCB_DLL_OBJ)
endif
	$(RM) $(LIBCB)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

#
# header file dependencies (incomplete)
#
$(OBJS):	cb.h
