#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# included Makefile to make building with Purify easier
#

ifneq ($(ARCH), SOLARIS)
USE_PURIFY=
endif

ifdef USE_PURIFY
PURIFY=purify
PUREOPTS=-best-effort -always-use-cache-dir=yes -cache-dir=/export/share/pub/richm/purify -follow-child-processes=yes -max-threads=256 -check-debug-timestamps=no
#PUREOPTS=-follow-child-processes=yes -max-threads=256 -check-debug-timestamps=no
endif # USE_PURIFY
