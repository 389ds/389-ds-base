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

OBJDEST = $(OBJDIR)/lib/libroles
LIBDIR = $(LIB_RELDIR)
ifndef INSTDIR
INSTDIR = c:/netscape/server4/
endif

include $(BUILD_ROOT)/nsdefs.mk
include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./roles.def
endif

ROLES_OBJS = roles_plugin.o roles_cache.o
OBJS = $(addprefix $(OBJDEST)/, $(ROLES_OBJS)) 

ROLES_DLL = roles-plugin

INCLUDES += -I../../slapd  -I../../../include
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

# DBDB this is clearly all nonsense: the libraries this thing links with should not depend on the platform.
# However, for now I make this AIX-specific change and leave the NT-specifc stuff in place (I think it came
# from the makefile I copied to make this one. After build 3, fix this.

ifeq ($(ARCH), WINNT)
EXTRA_LIBS_DEP +=   $(LIBSLAPD)
EXTRA_LIBS += $(NSPRLINK)  $(LIBSLAPD) $(LDAP_LIBAVL)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), WINNT)
ROLES_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), AIX)
LD=ld
EXTRA_LIBS += $(NSPRLINK) $(LIBSLAPD) $(LDAP_LIBAVL)
endif

ROLES=	$(addprefix $(LIBDIR)/, $(ROLES_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(ROLES)

ifeq ($(ARCH), WINNT)
$(ROLES): $(OBJS) $(ROLES_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(ROLES_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(ROLES): $(OBJS) $(ROLES_DLL_OBJ) 
	$(LINK_DLL) $(ROLES_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(ROLES_DLL_OBJ)
endif
	$(RM) $(ROLES)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

# Target to push the built binary to an installed server
#ROLES_PUSH = $(addprefix $(INSTDIR)lib/, $(notdir $(ROLES)))
#push: $(ROLES_PUSH)

#$(ROLES_PUSH): $(ROLES)
#	cp $(ROLES) $(ROLES_PUSH)
