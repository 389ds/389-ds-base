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

OBJDEST = $(OBJDIR)/lib/libcos
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./cos.def
endif

COS_OBJS =	cos.o cos_cache.o
OBJS = $(addprefix $(OBJDEST)/, $(COS_OBJS)) 

COS_DLL = cos-plugin

INCLUDES += -I../../slapd  -I../../../include
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=   $(LIBSLAPD) $(NSPR_DEP) $(LDAPSDK_DEP)
EXTRA_LIBS += $(NSPRLINK)  $(LIBSLAPD) $(LDAP_SDK_LIBLDAP_DLL)
COS_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS_DEP +=   $(LIBSLAPD) $(NSPR_DEP) $(LDAPSDK_DEP)
EXTRA_LIBS += $(LIBSLAPDLINK)  $(NSPRLINK)  $(LDAP_SDK_LIBLDAP_DLL)
LD=ld
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

COS=	$(addprefix $(LIBDIR)/, $(COS_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(COS)

ifeq ($(ARCH), WINNT)
$(COS): $(OBJS) $(COS_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(COS_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(COS): $(OBJS) $(COS_DLL_OBJ) 
	$(LINK_DLL) $(COS_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(COS_DLL_OBJ)
endif
	$(RM) $(COS)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)
