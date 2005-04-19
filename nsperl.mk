#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

NSPERL_RELDATE := 20020626
NSPERL_VERSION := nsPerl5.6.1
NSPERL_COMPONENT_DIR = $(COMPONENTS_DIR)/nsPerl/$(NSPERL_RELDATE)/$(NSOBJDIR_NAME_32)
# default; will be redefined below for specific platform
#PERL=$(NSPERL_COMPONENT_DIR)/lib/$(NSPERL_VERSION)/nsperl
PERL=/share/builds/sbstools/nsPerl/$(NSPERL_RELDATE)/$(NSOBJDIR_NAME_32)/nsperl
ifeq ($(BUILD_ARCH), WINNT)
PERL=nsperl
endif

ifdef USE_OLD_NTPERL
PERL=perl
endif

ifdef USE_PERL_FROM_PATH
PERL = $(shell perl -e 'print "$$\n"')
endif

NSPERL_ZIP_FILE = nsperl561.zip

# This makefile sets up the environment so that we can build and link
# perl xsubs.  It assumes that you have a perl base directory that has
# a bin and lib subdir and that which perl yields base dir/bin/perl[.exe]
# also, this is only really necessary for NT, since this usually just
# works in the NFS world of unix
# for unix, we derive the paths from the Config information
ifdef USE_OLD_NTPERL
PERL_EXE = $(shell $(PERL) -e '($$foo = $$) =~ s@\\@/@g ; print "$$foo\n"')
PERL_EXENT = $(subst \,/,$(PERL_EXE))
PERL_BASEDIR = $(dir $(PERL_EXENT))
PERL_ROOT = $(subst /bin/,,$(PERL_BASEDIR))
IS_ACTIVESTATE = $(shell $(PERL) -v | grep -i activestate)
else
PERL_CONFIG = $(shell $(PERL) -e 'use Config; foreach $$item (qw(installprivlib installarchlib installsitelib installsitearch prefixexp)) { ($$foo = $$Config{$$item}) =~ s@\\@/@g ; print "$$foo "; } print "\\\n"')
PERL_LIB = $(word 1, $(PERL_CONFIG))
PERL_ARCHLIB = $(word 2, $(PERL_CONFIG))
SITELIB = $(word 3, $(PERL_CONFIG))
SITEARCH = $(word 4, $(PERL_CONFIG))
PERL_ROOT = $(word 5, $(PERL_CONFIG))
endif

ifdef USE_OLD_NTPERL
PERL_LIB = $(PERL_ROOT)/lib
PERL_ARCHLIB = $(PERL_LIB)
PERL_SITE = site
SITELIB = $(PERL_ROOT)/$(PERL_SITE)/lib
SITEARCH = $(SITELIB)
endif

INSTALLSITEARCH = $(SITEARCH)
INSTALLSITELIB = $(SITELIB)
SITEARCHEXP = $(SITEARCH)
SITELIBEXP = $(SITELIB)
XSUBPPDIR = $(PERL_LIB)/ExtUtils
XSUBPP = $(XSUBPPDIR)/xsubpp
XSPROTOARG =
XSUBPPDEPS = $(XSUBPPDIR)/typemap
XSUBPPARGS = -typemap $(XSUBPPDIR)/typemap
PERL_INC = -I$(PERL_ARCHLIB)/CORE

SITEHACK = $(subst $(PERL_ROOT)/,,$(SITELIB))
ARCHHACK = $(subst $(PERL_ROOT)/,,$(PERL_ARCHLIB))

ifeq ($(ARCH), WINNT)
ifdef IS_ACTIVESTATE
# C compilation/linking does not work for activestate; force C++
PERL_CFLAGS = -TP -D_CONSOLE -DNO_STRICT -DPERL_OBJECT
ifeq ($(DEBUG), full)
PERL_CFLAGS += -DNDEBUG
endif
LIBPERL_A = /LIBPATH:$(PERL_ARCHLIB)/CORE perlCAPI.lib perlcore.lib PerlCRT.lib
else
LIBPERL_A = /LIBPATH:$(PERL_ARCHLIB)/CORE perl56.lib
endif
else
ifeq ($(DEBUG), full)
PERL_CFLAGS = -UDEBUG
endif
LIBPERL_A = -L$(PERL_ARCHLIB)/CORE -lperl
endif
