#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
NSPR20     = true

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
	$(LINK_DLL) $(LD_EXTRAS) /DEF:"nsldapagt_nt.def" /VERSION:"7" \
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

