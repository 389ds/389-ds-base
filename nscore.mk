#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Shared rules for modules depending on nscore

# AIX uname
AIXOS_VERSION := $(shell uname -v)
AIXOS_RELEASE := $(shell uname -r)
AIXOS = $(AIXOS_VERSION).$(AIXOS_RELEASE)

#
# Compiler
#
# Windows NT
ifeq ($(ARCH), WINNT)
  CFLAGS += -DOS_windows -DNT 
  PROCESSOR := $(shell uname -p)
  ifeq ($(PROCESSOR), I386)
    CFLAGS += -DCPU_x86
  else
  ifeq ($(PROCESSOR), ALPHA)
    CFLAGS += -DCPU_alpha
  else
  ifeq ($(PROCESSOR), MIPS)
    CFLAGS += -DCPU_mips
  endif
  endif
  endif
endif
# Solaris
ifeq ($(ARCH), SOLARIS)
  CFLAGS += -DCPU_sparc -DOS_solaris
endif
#Solarisx86
ifeq ($(ARCH), SOLARISx86)
  CFLAGS += -DOS_solaris
endif

# HP-UX
ifeq ($(ARCH), HPUX)
  CFLAGS += -DCPU_hppa -DOS_hpux
  CFLAGS += -D_NO_THREADS_
endif
# AIX
ifeq ($(ARCH), AIX)
  CFLAGS += -DCPU_powerpc -DOS_aix
endif
# IRIX
ifeq ($(ARCH), IRIX)
  CFLAGS += -DCPU_mips -DOS_irix
  CFLAGS += -D_NO_THREADS_
endif
# OSF1
ifeq ($(ARCH), OSF1)
  CFLAGS += -DCPU_alpha -DOS_osf1
endif
ifeq ($(ARCH), UNIXWARE)
  CFLAGS += -DSYSV -DSVR4 -DCPU_i486 -DOS_unixware
endif
ifeq ($(ARCH), UnixWare)
  CFLAGS += -DSYSV -DCPU_i486 -DOS_unixware
  SYSV_REL := $(shell uname -r)
ifeq ($(SYSV_REL), 5)
  CFLAGS += -DSVR5
else
  CFLAGS += -DSVR4
endif
endif
ifeq ($(ARCH), SCO)
  CFLAGS += -DSYSV -DCPU_i486 -DOS_sco
endif
ifeq ($(ARCH), NCR)
  CFLAGS += -DSYSV -DSVR4 -DCPU_i486 -DOS_ncr
endif
