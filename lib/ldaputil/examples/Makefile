#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Makefile for certmap example program.
#

#
# Please set the ARCH variable to one of the following:
# SOLARIS, IRIX, HPUX
#
ARCH = 


#
# Please set the SROOT to be same as your server root
#
SROOT = 

#
# Uncomment the following if you need the debug build
#
#COMMON_DEFS = -g

ifndef ARCH
arch:
	@echo "Please edit the Makefile and set the variable: ARCH"
	@exit 1
endif

ifndef SROOT
sroot:
	@echo "Please edit the Makefile and set the server root variable: SROOT"
	@exit 1
endif

ifeq ($(ARCH), SOLARIS)
CC_CMD = cc -DSOLARIS -D_REENTRANT
LD_SHAREDCMD = ld -G
endif

ifeq ($(ARCH), IRIX)
CC_CMD = cc
LD_SHAREDCMD = ld -32 -shared
endif

ifeq ($(ARCH), HPUX)
  BIN = certmap.sl
else
  BIN = certmap.so
endif

OBJS = init.o plugin.o

INCLUDE_FLAGS=-I. -I$(SROOT)/include

INC_FILES = \
	$(SROOT)/include/certmap.h \
	$(SROOT)/include/ldap.h \
	$(SROOT)/include/lber.h

all: $(BIN)

$(INC_FILES):
	@echo
	@echo "To extend the Certificate to LDAP entry mapping by"
	@echo "writing your own functions, you need to download the"
	@echo "Certmap API (version 1.0) and LDAP SDK (version 1.0)."
	@echo "Please download these from http://???"
	@echo "Make sure the following files exist:"
	@echo "\t$(SROOT)/include/certmap.h"
	@echo "\t$(SROOT)/include/ldap.h"
	@echo "\t$(SROOT)/include/lber.h"
	@echo
	@exit 1

$(BIN):	$(INC_FILES) $(OBJS)
	$(LD_SHAREDCMD) $(OBJS) -o $@ $(EXTRA_LDDEFINES)

certmap.dll: $(OBJS)
	$(LD_SHAREDCMD) $(OBJS) -o $@ $(EXTRA_LDDEFINES)

.c.o:
	$(CC_CMD) $(COMMON_DEFS) $(INCLUDE_FLAGS) -c $< 

clean:
	rm -f $(OBJS) certmap.so $(EXTRA_CLEAN)
