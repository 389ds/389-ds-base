#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
# included Makefile to make building with Quantify easier
#

ifneq ($(ARCH), SOLARIS)
USE_QUANTIFY=
endif

ifdef USE_QUANTIFY
QUANOPTS=-best-effort -always-use-cache-dir=yes -follow-child-processes=yes -max-threads=256 -check-debug-timestamps=no
QUANTIFY=quantify $(QUANOPTS)
endif # USE_QUANTIFY
