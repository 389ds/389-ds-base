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

OBJDEST = $(OBJDIR)/lib/referint-plugin
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./referint.def
endif

REFERINT_OBJS =	referint.o 
OBJS = $(addprefix $(OBJDEST)/, $(REFERINT_OBJS)) 

INCLUDES += -I../../slapd  -I../../../include
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPLINK_DEP) $(NSPRLINK_DEP)
EXTRA_LIBS += $(LIBSLAPD) $(LDAPLINK) $(NSPRLINK)

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(SECURITY_DEP)
EXTRA_LIBS += $(SECURITYLINK)
endif

ifeq ($(ARCH), WINNT)
REFERINT_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), AIX)
LD=ld
endif

REFERINT=	$(addprefix $(LIBDIR)/, $(REFERINT_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(REFERINT)

ifeq ($(ARCH), WINNT)
$(REFERINT): $(OBJS) $(REFERINT_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(REFERINT_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(REFERINT): $(OBJS) $(REFERINT_DLL_OBJ) 
	$(LINK_DLL) $(REFERINT_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(REFERINT_DLL_OBJ)
endif
	$(RM) $(REFERINT)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
