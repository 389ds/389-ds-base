#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
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

