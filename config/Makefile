#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#! gmake

DEPTH	= ..

HSRCS	= pathsub.h
CSRCS	= nsinstall.c pathsub.c

PLSRCS	= nfspwd.pl revdepth.pl

ifneq ($(subst /,_,$(shell uname -s)),WINNT)
PROGRAM	= nsinstall
OBJS	= $(CSRCS:.c=.o)
endif

TARGETS	= $(PROGRAM) $(PLSRCS:.pl=)

# IMPORTANT: Disable NSBUILDROOT for this directory only, otherwise we have
# a recursive rule for finding nsinstall and the perl scripts
ifdef NSBUILDROOT
override NSBUILDROOT :=
endif

include $(DEPTH)/config/rules.mk

# Redefine MAKE_OBJDIR for just this directory
define MAKE_OBJDIR
if test ! -d $(@D); then rm -rf $(@D); mkdir $(@D); fi
endef

export:: $(TARGETS)
install:: $(TARGETS)
