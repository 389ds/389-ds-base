#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for include/public/nsacl public header files

MCOM_ROOT = ../../../..
MODULE=netsiteIncludeNsacl

include ../../../nsdefs.mk

HDRDEST=$(OBJDIR)/include/nsacl

PREFIX=copyrght.h


NOSTDSTRIP=true
NOSTDDEPEND=true

HDRS=$(wildcard *.h)

BINS=$(addprefix $(HDRDEST)/,$(HDRS))

all: $(HDRDEST) $(BINS)

$(HDRDEST):
	mkdir -p $(HDRDEST)

strip:
depend:

include ../../../nsconfig.mk

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h
