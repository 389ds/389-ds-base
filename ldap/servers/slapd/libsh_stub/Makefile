#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# GNU Makefile for libsh_stub.so
#
LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/libsh_stub

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(LDAP_SRC)/nsdeps.mk


LIBSH_STUB_C=	libsh_stub.c

SHARE_LIB = $(addprefix $(OBJDEST)/, libsh_stub.so)

# share lib
CFLAGS = -G $(SLCFLAGS)

# link libmtmalloc.so when built on 5.8 or 5.9
ifeq ($(ARCH), SOLARIS)
  ifeq ($(NSOS_RELEASE), 5.8)
	LINKFLAGS = -lmtmalloc
  endif
  ifeq ($(NSOS_RELEASE), 5.9)
	LINKFLAGS = -lmtmalloc
  endif
  LIBSH_STUB = libsh_stub
else
  # do nothing on non-SOLARIS platforms
  LIBSH_STUB = no_op
endif

all: $(LIBSH_STUB)

.PHONY: libsh_stub no_op

libsh_stub: $(OBJDEST) $(SHARE_LIB)

$(SHARE_LIB): $(LIBSH_STUB_C)
	$(CC) -o $(SHARE_LIB) $(CFLAGS) $(LIBSH_STUB_C) $(LINKFLAGS)

no_op:
	-@echo libsh_stub.so is not built on $(ARCH) platform

$(OBJDEST) :
	$(MKDIR) $@

veryclean: clean

clean:
	$(RM) $(SHARE_LIB)
