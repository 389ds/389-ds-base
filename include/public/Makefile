#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# Makefile for netsite.h

BUILD_ROOT = ../..
MODULE=netsiteInclude

include $(BUILD_ROOT)/nsdefs.mk

HDRDEST=$(OBJDIR)/include

PREFIX=../copyrght.h


NOSTDSTRIP=true
NOSTDDEPEND=true

HDRS=netsite.h nsapi.h

BINS=$(addprefix $(HDRDEST)/,$(HDRS))

all: stuff

strip:
depend:

include $(BUILD_ROOT)/nsconfig.mk

ifeq ($(NSAPI_CAPABLE), true)

stuff: $(HDRDEST) $(BINS) sub-hdrs

$(HDRDEST):
	mkdir -p $(HDRDEST)

ifeq ($(PRODUCT), "Netscape Proxy Server")
sub-hdrs:
	cd base; $(MAKE) $(MAKEFLAGS)
	cd frame; $(MAKE) $(MAKEFLAGS)
	cd libproxy; $(MAKE) $(MAKEFLAGS)
else
sub-hdrs:
	cd base; $(MAKE) $(MAKEFLAGS) 
	cd frame; $(MAKE) $(MAKEFLAGS) 
	cd nsacl; $(MAKE) $(MAKEFLAGS) 
endif

$(HDRDEST)/%.h: %.h
	cat $(PREFIX) $< > $(HDRDEST)/$*.h

else
stuff:

endif
