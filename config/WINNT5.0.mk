#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for WINNT 5.0
#

CC=cl
CCC=cl
LINK = link
RANLIB = echo
BSDECHO = echo

OTHER_DIRT = 
GARBAGE = vc20.pdb

ifdef DEBUG_RUNTIME
RTLIBFLAGS:=-MDd
else
RTLIBFLAGS:=-MD
endif

PROCESSOR := $(shell uname -p)
USE_KERNEL_THREADS=1
_PR_USECPU=1
ifeq ($(PROCESSOR), I386)
CPU_ARCH = x386
OS_CFLAGS = $(OPTIMIZER) -GT $(RTLIBFLAGS) -W3 -nologo -D_X86_ -Dx386 -D_WINDOWS -DWIN32 -DHW_THREADS
else 
ifeq ($(PROCESSOR), MIPS)
CPU_ARCH = MIPS
#OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_MIPS_ -D_WINDOWS -DWIN32 -DHW_THREADS
OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_WINDOWS -DWIN32 -DHW_THREADS
else 
ifeq ($(PROCESSOR), ALPHA)
CPU_ARCH = ALPHA
OS_CFLAGS = $(OPTIMIZER) $(RTLIBFLAGS) -W3 -nologo -D_ALPHA_=1 -D_WINDOWS -DWIN32 -DHW_THREADS
else 
CPU_ARCH = processor_is_undefined
endif
endif
endif

ifeq ($(SERVER_BUILD), 1)
OS_CFLAGS += -DSERVER_BUILD
endif

OS_DLLFLAGS = -nologo -DLL -SUBSYSTEM:WINDOWS -MAP -PDB:NONE
OS_LFLAGS = -nologo -PDB:NONE -INCREMENT:NO -SUBSYSTEM:console
OS_LIBS = kernel32.lib user32.lib gdi32.lib winmm.lib wsock32.lib advapi32.lib

OS_DEFS= SERVER_BUILD=$(SERVER_BUILD) NSPR_VERSION=$(VERSION) NS_PRODUCT=$(NS_PRODUCT)
