#
# PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
# license terms. Copyright 2001 Sun Microsystems, Inc.
# Some preexisting portions Copyright 2001 Netscape Communications Corp.
# All rights reserved.
# 
LDAP_SRC = ../../..
MCOM_ROOT = ../../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip
NSPR20=true	# probably should be defined somewhere else (not sure where)

OBJDEST = $(OBJDIR)/lib/libhttpclient
LIBDIR = $(LIB_RELDIR)

include $(MCOM_ROOT)/ldapserver/nsdefs.mk
include $(MCOM_ROOT)/ldapserver/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk

ifeq ($(ARCH), WINNT)
DEF_FILE:=./http.def
endif

HTTP_OBJS =	http_client.o http_impl.o

OBJS = $(addprefix $(OBJDEST)/, $(HTTP_OBJS)) 

HTTP_DLL = http-client-plugin

INCLUDES += -I../../slapd  -I../../../include

CFLAGS+=$(SLCFLAGS) -DSLAPD_LOGGING 

ifeq ($(ARCH), WINNT)
CFLAGS+=-D_WIN32 -DXP_WIN -DXP_WIN32
EXTRA_LIBS_DEP +=  $(LIBSLAPD_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(NSPRLINK) $(LIBSLAPD) $(SECURITYLINK)
HTTP_DLL_OBJ = $(addprefix $(OBJDEST)/, dllmain.o)
endif

ifeq ($(ARCH), HPUX)
EXTRA_LIBS_DEP += $(LIBSLAPD_DEP) $(LDAPSDK_DEP) $(NSPR_DEP) $(SECURITY_DEP)
EXTRA_LIBS += $(DYN_NSHTTPD) $(ADMINUTIL_LINK) $(LDAPLINK) $(SECURITYLINK) $(NSPRLINK) $(ICULINK)
endif

ifeq ($(ARCH), AIX)
LD=ld
EXTRA_LIBS += $(LIBSLAPD) 
endif

HTTP=	$(addprefix $(LIBDIR)/, $(HTTP_DLL).$(DLL_SUFFIX))

clientSDK: 

all:	$(OBJDEST) $(LIBDIR) $(HTTP)

ifeq ($(ARCH), WINNT)
$(HTTP): $(OBJS) $(HTTP_DLL_OBJ) $(DEF_FILE)
	$(LINK_DLL) $(HTTP_DLL_OBJ) $(EXTRA_LIBS) /DEF:$(DEF_FILE)
else
$(HTTP): $(OBJS) $(HTTP_DLL_OBJ) 
	$(LINK_DLL) $(HTTP_DLL_OBJ) $(EXTRA_LIBS)
endif


veryclean: clean

clean:
	$(RM) $(OBJS)
ifeq ($(ARCH), WINNT)
	$(RM) $(HTTP_DLL_OBJ)
endif
	$(RM) $(HTTP)

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(LIBDIR):
	$(MKDIR) $(LIBDIR)
