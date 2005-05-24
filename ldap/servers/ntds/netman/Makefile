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
LDAP_SRC = ../../..
BUILD_ROOT = ../../../..

NOSTDCLEAN=true # don't let nsconfig.mk define target clean
NOSTDSTRIP=true # don't let nsconfig.mk define target strip

OBJDEST = $(OBJDIR)/lib/netman
LIBDIR = $(LIB_RELDIR)

include $(BUILD_ROOT)/nsconfig.mk
include $(LDAP_SRC)/nsldap.mk
include $(LDAP_SRC)/javarules.mk
JAR=jar
SWIG_OPTS=-v -outdir $(OBJDEST)
JNETDEST=$(JAVA_DEST_DIR)/jnetman
JAVA_HOME=$(JDKPRELIB)/..


CPP_PROJ=$(CFLAGS)

all:	$(OBJDEST) $(OBJDEST)/jnetman_wrap.cxx $(OBJDEST)/jnetman.dll  $(JAVA_DEST_DIR)/ntds/jnetman.jar 

$(OBJDEST):
	$(MKDIR) $(OBJDEST)

$(OBJDEST)/jnetman_wrap.cxx:
	$(SWIG_EXE) $(SWIG_OPTS) -java -package org.bpi.jnetman -c++ jnetman.i
	$(MV) jnetman_wrap.cxx $(OBJDEST)

$(JAVA_DEST_DIR)/ntds/jnetman.jar:	$(OBJDEST)/jnetman_wrap.cxx
	$(MKDIR) $(JAVA_DEST_DIR)/ntds 
	$(JAVAC) -d $(JAVA_DEST_DIR)/ntds $(OBJDEST)/*.java
	( cd  $(JAVA_DEST_DIR)/ntds && jar cf $(JAVA_DEST_DIR)/ntds/jnetman.jar org)
	#$(JAR) cf $(JAVA_DEST_DIR)/ntds/jnetman.jar $(JAVA_DEST_DIR)/ntds 


$(OBJDEST)/jnetman.dll:  $(OBJDEST)/netman.obj $(OBJDEST)/jnetman_wrap.obj
	$(LINK_DLL) /out:$(OBJDEST)/jnetman.dll    $(EXTRA_LIBS) netapi32.lib  $(OBJDEST)/netman.obj $(OBJDEST)/jnetman_wrap.obj

$(OBJDEST)/netman.obj:
	$(CC) -c netman.cpp $(CFLAGS)   $(MCC_INCLUDE) -Fo$(OBJDEST)/ $(CBSCFLAGS)

$(OBJDEST)/jnetman_wrap.obj:
	$(CC) -c  $(OBJDEST)/jnetman_wrap.cxx $(CFLAGS)   $(MCC_INCLUDE) -Fo$(OBJDEST)/ $(CBSCFLAGS) /I$(JAVA_HOME)/include /I$(JAVA_HOME)/include/win32 /I.

veryclean: clean

clean:
	$(RM) $(OBJDEST)/jnetman_wrap.cxx 
	$(RM) $(OBJDEST)/jnetman.dll  
	$(RM) $(OBJDEST)/jnetman.lib  
	$(RM) $(OBJDEST)/*.obj
	$(RM) $(OBJDEST)/*.java
	$(RM) $(JAVA_DEST_DIR)/ntds/jnetman.jar 



