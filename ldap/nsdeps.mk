#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
#  build dependency lists if necessary, then make 'build.mk'
#
#  only build dependency lists on platforms that it works on...
#


ifeq ($(ARCH), WINNT)
# windows can't make dot-files:
DEPFILE = ./deps
$(OBJDIR)/mkdep: $(LDAP_SRC)/servers/slapd/tools/mkdep.c
	$(CC) /Ox /DWINNT /Fe$(OBJDIR)/mkdep.exe \
		$(LDAP_SRC)/servers/slapd/tools/mkdep.c
else
DEPFILE = ./.deps
$(OBJDIR)/mkdep: $(LDAP_SRC)/servers/slapd/tools/mkdep.c
	$(CC) -o $(OBJDIR)/mkdep $(LDAP_SRC)/servers/slapd/tools/mkdep.c
endif

ifeq ($(RECURSIVE_DEP), yes)
$(DEPFILE): *.h *.c
	@echo Cant seem to create $(DEPFILE), time to die.
	@exit 1
else
$(DEPFILE): *.h *.c
	@echo Rebuilding dependency lists...
	$(OBJDIR)/mkdep -o $(OBJDEST) *.h *.c >$(DEPFILE)
	$(MAKE) RECURSIVE_DEP=yes
endif

#
# you can override these from the command line
#
ifeq ($(ARCH), SOLARIS)
USE_DEPS = no
endif
ifeq ($(ARCH), Linux)
USE_DEPS = no
endif
ifeq ($(ARCH), WINNT)
USE_DEPS = no
endif


# automatic dependency checking?
ifeq ($(USE_DEPS), yes)
  ifeq ($(RECURSIVE_DEP), yes)
    include $(DEPFILE)
  else
    BUILD_DEP = $(OBJDIR)/mkdep $(DEPFILE)
  endif
endif
