#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
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
