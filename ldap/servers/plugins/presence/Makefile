#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libpresence
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./presence.def
endif

PRESENCE_OBJS =	presence.o
OBJS = $(addprefix $(OBJDEST)/, $(PRESENCE_OBJS)) 

PRESENCE_DLL = presence-plugin

INCLUDES += -I../http -I../../slapd  -I../../../include  
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=   $(LIBSLAPD) $(NSPR_DEP) $(LDAPSDK_DEP)
EXTRA_LIBS_DEP +=   $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS += $(NSPRLINK)  $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
EXTRA_LIBS += $(LDAP_COMMON_LIBS)
PRESENCE_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP +=   $(LIBSLAPD) $(NSPR_DEP) $(LDAPSDK_DEP)
EXTRA_LIBS_DEP +=   $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS += $(LIBSLAPDLINK)  $(NSPRLINK)  $(LDAP_SDK_LIBLDAP_DLL)
EXTRA_LIBS += $(LDAP_COMMON_LIBS)
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS_DEP +=   $(LDAP_COMMON_LIBS_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
EXTRA_LIBS += $(LDAP_COMMON_LIBS)
endif

PRESENCE=	$(addprefix $(LIBDIR)/, $(PRESENCE_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(PRESENCE)

ifeq ($(ARCH), WINNT)
$(PRESENCE): $(OBJS) $(PRESENCE_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(PRESENCE_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(PRESENCE): $(OBJS) $(PRESENCE_DLL_OBJ) 
	$(LINK_DLL) $(PRESENCE_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(PRESENCE_DLL_OBJ)
endif
	$(RM) $(PRESENCE)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)
