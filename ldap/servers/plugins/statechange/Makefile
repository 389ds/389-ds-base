#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libstatechange
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./statechange.def
endif

STATECHANGE_OBJS =	statechange.o
OBJS = $(addprefix $(OBJDEST)/, $(STATECHANGE_OBJS)) 

STATECHANGE_DLL = statechange-plugin

INCLUDES += -I../../slapd  -I../../../include
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=   $(LIBSLAPD)
EXTRA_LIBS += $(NSPRLINK)  $(LIBSLAPD) 
STATECHANGE_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), AIX)
LD=ld
EXTRA_LIBS += $(LIBSLAPD) 
endif

STATECHANGE=	$(addprefix $(LIBDIR)/, $(STATECHANGE_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(STATECHANGE)

ifeq ($(ARCH), WINNT)
$(STATECHANGE): $(OBJS) $(STATECHANGE_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(STATECHANGE_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(STATECHANGE): $(OBJS) $(STATECHANGE_DLL_OBJ) 
	$(LINK_DLL) $(STATECHANGE_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(STATECHANGE_DLL_OBJ)
endif
	$(RM) $(STATECHANGE)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)
