#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
ifdef WEBSERVER_LANGS
LANG_LOOP=							\
  @for d in $(WEBSERVER_LANGS); do				\
    if test ! -d $$d; then					\
      echo Directory $$d does not exist. Creating...;		\
      mkdir -p $$d;						\
    fi;								\
    echo cd $$d;						\
    cd $$d;							\
    echo $(MAKE) -f ../Makefile $(MAKEFLAGS) INT_SUBDIR=1;	\
    $(MAKE) -f ../Makefile $(MAKEFLAGS) INT_SUBDIR=1;		\
    cd ..;							\
  done
else
LANG_LOOP= @echo "No foreign languages in this build"
endif

