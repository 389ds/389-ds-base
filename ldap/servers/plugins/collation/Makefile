#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
LDAP_SRC= ../../..
BUILD_ROOT= ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST= $(OBJDIR)/lib/liblcoll
LIBDIR= $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

INCLUDES+= -I../../slapd  -I../../../include 
CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING

COLLATION_OBJS= collate.o config.o orfilter.o

ifeq ($(ARCH), WINNT)
COLLATION_OBJS+= debug.o
COLLATION_DLL_OBJ=$(addprefix $(OBJDEST)/, dllmain.o)
DEF_FILE:=./collation.def
EXTRA_LIBS+= $(NSPRLINK) $(LDAP_SDK_LIBLDAP_DLL) $(LIBSLAPD) 
EXTRA_LIBS_DEP+= $(LIBSLAPD_DEP)
EXTRA_LIBS_DEP+=$(LDAPSDK_DEP)
endif

# INCLUDES+= -I. -I$(ACLINC) -I$(BUILD_ROOT)/lib

# ICU stuff
INCLUDES+= $(ICU_INCLUDE)
EXTRA_LIBS+=$(ICULINK) 

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), AIX)
EXTRA_LIBS+= $(LIBSLAPDLINK) $(NSPRLINK) $(LDAPLINK)
EXTRA_LIBS_DEP+= $(LIBSLAPD_DEP) 
EXTRA_LIBS_DEP+=$(LDAPSDK_DEP)
LD=ld
endif

OBJS= $(addprefix $(OBJDEST)/, $(COLLATION_OBJS)) 
COLLATION= $(addprefix $(LIBDIR)/, $(COLLATION_DLL).$(DLL_SUFFIX))

clientSDK: 

all: $(OBJDEST) $(LIBDIR) $(COLLATION)
ifeq (0, 1)
# Where the heck did the compiler options come from?
	@echo ARCH=$(ARCH)
	@echo DEBUG=$(DEBUG)
	@echo BUILD_OPT=$(BUILD_OPT)
	@echo CFLAGS=$(CFLAGS)
	@echo "    MCC_DEBUG="$(MCC_DEBUG)
	@echo "    PLATFORMCFLAGS="$(PLATFORMCFLAGS)
	@echo "    ACFLAGS="$(ACFLAGS)
	@echo "        EXTRACFLAGS="$(EXTRACFLAGS)
	@echo "        UNPROTOCFLAGS="$(UNPROTOCFLAGS)
	@echo "    SLCFLAGS="$(SLCFLAGS)
	@echo "ALDFLAGS="$(ALDFLAGS)
	@echo "DLL_LDFLAGS="$(DLL_LDFLAGS)
	@echo "DLL_EXPORT_FLAGS="$(DLL_EXPORT_FLAGS)
endif

ifeq ($(ARCH), WINNT)
$(COLLATION): $(OBJS) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS_DEP) $(DEF_FILE)
	$(LINK_DLL) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
ifeq ($(ARCH), AIX)
$(COLLATION): $(OBJS) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(COLLATION_DLL_OBJ) $(EXTRA_LIBS)
else
$(COLLATION): $(OBJS) $(EXTRA_LIBS_DEP)
	$(LINK_DLL) $(EXTRA_LIBS)
endif
endif

veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(COLLATION_DLL_OBJ)
endif
	$(RM) $(COLLATION)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)
