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
#############################################################################
#                       SNMP NT Subagent Common Rulesets                    #
#############################################################################

BUILD_ROOT   =   ../../../..
MSRV_ROOT   =   ../../..
SLAPD_ROOT  =   $(MSRV_ROOT)/servers/slapd
SNMP_ROOT   =   $(MSRV_ROOT)/servers/snmp
LIBDEST     =   $(OBJDIR)/obj
BINDIR      =   $(LDAP_SERVER_RELDIR)

NOSTDCLEAN = true # don't let nsconfig.mk define target clean
NOSTDSTRIP = true # don't let nsconfig.mk define target strip

include         $(BUILD_ROOT)/nsconfig.mk
include         $(MSRV_ROOT)/nsldap.mk

OBJ_SUFFIX  = obj

INCLUDES    +=   -I../../slapd -I$(BUILD_ROOT)/ldap/include

CCOPTS      +=  $(CFLAGS) $(DLL_CFLAGS) $(MCC_INCLUDE) $(INCLUDES) -I$(SNMP_ROOT) 
CXXOPTS     +=  $(CFLAGS) $(DLL_CXXFLAGS) $(MCC_INCLUDE) $(INCLUDES) -I$(SNMP_ROOT) 
 
EXTRA_LIBS =  advapi32.lib user32.lib snmpapi.lib mgmtapi.lib $(LDAP_SDK_LIBLDAP_DLL) 

#############################################################################
#                   SNMP NT Subagent Core Components                        #
#############################################################################

LOCAL_SRCS      =  agtmmap.c      \
				   nsldapagt_nt.c \
                   nsldapmib_nt.c

SNMP_SRCS       =  $(LOCAL_SRCS)

LOCAL_INCS      =  agtmmap.h      \
				   nsldapagt_nt.h \
                   nsldapmib_nt.h

SNMP_INCS       =  $(LOCAL_INCS)

SNMP_OBJS       =  $(addprefix $(LIBDEST)/, \
			agtmmap.$(OBJ_SUFFIX) \
			nsldapagt_nt.$(OBJ_SUFFIX) \
			nsldapmib_nt.$(OBJ_SUFFIX))


SNMP_LIB	=  ns-ldapagt.$(DLL_SUFFIX)
SNMP_IMPLIB	=  ns-ldapagt.$(LIB_SUFFIX)

#############################################################################
#                  SNMP NT Subagent Build Rules                             #
#############################################################################

# Now we get into the actual build rules
AGTMMAP = agtmmap

all: $(LDAP_SERVER_RELDIR) $(AGTMMAP) $(BINDIR)/$(SNMP_LIB)

over: clean all

$(AGTMMAP): $(SLAPD_ROOT)/$(AGTMMAP).c $(SLAPD_ROOT)/$(AGTMMAP).h
	$(CP) $(SLAPD_ROOT)/$(AGTMMAP).c .
	$(CP) $(SLAPD_ROOT)/$(AGTMMAP).h .

$(SNMP_OBJS): $(SNMP_SRCS) $(SNMP_INCS)

$(BINDIR)/$(SNMP_LIB): $(SNMP_OBJS) $(SNMP_RES) Makefile
	$(LINK_DLL) $(LD_EXTRAS) /DEF:"nsldapagt_nt.def" /VERSION:"1" \
	$(SNMP_OBJS) $(EXTRA_LIBS)

$(LIBDEST)/%.$(OBJ_SUFFIX): %.cxx
	$(CCP) -c $(CXXOPTS) $< -Fo$(LIBDEST)/$*.$(OBJ_SUFFIX)
 
$(LIBDEST)/%.$(OBJ_SUFFIX): %.c
	$(CC) -c $(CCOPTS) $< -Fo$(LIBDEST)/$*.$(OBJ_SUFFIX)

$(LIBDEST)/%.$(OBJ_SUFFIX): $(SNMP_ROOT)/%.cxx
	$(CCP) -c $(CXXOPTS) $< -Fo$(LIBDEST)/$*.$(OBJ_SUFFIX)
 
$(LIBDEST)/%.$(OBJ_SUFFIX): $(SNMP_ROOT)/%.c
	$(CC) -c $(CCOPTS) $< -Fo$(LIBDEST)/$*.$(OBJ_SUFFIX)

clean: localclean

localclean:
	$(RM) $(SNMP_OBJS) $(LIBDEST)/$(SNMP_LIB) $(LIBDEST)/$(SNMP_IMPLIB) $(AGTMMAP).c $(AGTMMAP).h

#############################################################################
#                            Depend Area                                    #
#############################################################################

# DO NOT DELETE THIS LINE -- make depend depends on it.

