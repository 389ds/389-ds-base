#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#! gmake

DEPTH = ../..

CSRCS = info.c		\
	path.c	\
	pmddeml.c	\
	registry.c	\
	service.c \
	tcpip.c \
	$(NULL)

OBJS = $(CSRCS:.c=.o)

LIBRARY = libnt.$(LIB_SUFFIX)

include $(DEPTH)/config/rules.mk

export:: $(TARGETS)
	$(INSTALL) -m 444 $(LIBRARY) $(DIST)/lib
