#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# Config stuff for SunOS5.8
#

SOL_CFLAGS	= -D_SVID_GETTOD -DSOLARIS_55_OR_GREATER

include $(DEPTH)/config/SunOS5.mk
