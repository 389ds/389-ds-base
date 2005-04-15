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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# modules.mk: defines rules for each module that any part of the systems 
# will require in dependency lists.
#
# The current module will not be defined, you must have set the variable
# MODULE before this file is included.


NS_LIBDIR=$(BUILD_ROOT)/lib
MCOM_LIBDIR=$(BUILD_ROOT)/lib
HTTPD=$(BUILD_ROOT)/httpd
PROXY=$(BUILD_ROOT)/proxy
BATMAN=$(BUILD_ROOT)/batman
MAIL=$(BUILD_ROOT)/mailserv2
NEWS=$(BUILD_ROOT)/news
CMS=$(BUILD_ROOT)/species
ROGUE=$(BUILD_ROOT)/lw/rogue

# ------------------------------- Modules --------------------------------


ifneq ($(MODULE), LibRegex)
LIBREGEX=regex
LIBRARY regex $(NS_LIBDIR)/libregex
endif

ifneq ($(MODULE), LibBase)
BASE=base $(LIBSI18N)
BASE_SSL=base-ssl
LIBRARY base $(NS_LIBDIR)/base
endif

ifneq ($(MODULE), LibAccess)
LIBACCESS=access $(BASE) $(LIBSI18N)
LIBRARY libaccess $(NS_LIBDIR)/libaccess
endif

ifneq ($(MODULE), LibLdapUtil)
LIBLDAPU=ldapu $(BASE)
LIBRARY libldapu $(NS_LIBDIR)/ldaputil
endif

ifneq ($(MODULE), LibHttpDaemon)
HTTPDAEMON=httpdaemon
HTTPDAEMON_SSL=httpdaemon-ssl
LIBRARY libhttpdaemon $(NS_LIBDIR)/httpdaemon
endif

ifneq ($(MODULE), LibFrame)
FRAME=frame $(BASE)
FRAME_SSL=frame-ssl $(BASE_SSL)
LIBRARY frame $(NS_LIBDIR)/frame
endif

ifneq ($(MODULE), LibProxy)
LIBPROXY=libproxy
LIBPROXY_SSL=libproxy-ssl
LIBRARY libproxy $(NS_LIBDIR)/libproxy
endif

ifneq ($(MODULE), LibSNMP)
LIBSNMP=libsnmp
LIBSNMP_SSL=libsnmp-ssl
LIBRARY libsnmp $(NS_LIBDIR)/libsnmp
endif

ifneq ($(MODULE), LibSAFs)
SAFS=safs
SAFS_SSL=safs-ssl
LIBRARY safs $(NS_LIBDIR)/safs
endif

ifneq ($(MODULE), LibAR)
LIBARES=ares
LIBRARY libares
endif

ifneq ($(MODULE), CGIUtils)
CGIUTILS=cgiutils
LIBRARY cgiutils $(NS_LIBDIR)/cgiutils
endif

ifneq ($(MODULE), LibAdmin)
LIBADMIN=admin
LIBRARY admin $(NS_LIBDIR)/libadmin
endif

ifneq ($(MODULE), LibAdminUtil)
#LIBADMINUTIL=adminutil
LIBRARY adminutil $(NS_LIBDIR)/libadminutil
endif

ifneq ($(MODULE), LibCrypt)
LIBCRYPT=crypt
LIBRARY crypt $(NS_LIBDIR)/libcrypt
endif

ifneq ($(MODULE), LibMessages)
LIBMESSAGES=messages
LIBRARY messages $(NS_LIBDIR)/libmessages
endif

ifneq ($(MODULE), LibNSCore)
LIBNSCORE=nscore
LIBRARY nscore $(NS_LIBDIR)/libnscore
endif

ifneq ($(MODULE), LibMsgDisp)
LIBNSCORE=msgdisp
LIBRARY msgdisp $(NS_LIBDIR)/libmsgdisp
endif

ifneq ($(MODULE), LibMetaData)
LIBNSCORE=metadata
LIBRARY metadata $(NS_LIBDIR)/libmetadata
endif

ifneq ($(MODULE), LibIr)
LIBNSCORE=ir
LIBRARY ir $(NS_LIBDIR)/libir
endif

ifneq ($(MODULE), LibDocLdr)
LIBNSCORE=docldr
LIBRARY docldr $(NS_LIBDIR)/libdocldr
endif

ifneq ($(MODULE), LibVLdr)
LIBNSCORE=vldr
LIBRARY vldr $(NS_LIBDIR)/libvldr
endif

ifneq ($(MODULE), LibsI18N)
LIBSI18N=si18n
LIBRARY si18n $(NS_LIBDIR)/libsi18n
endif

ifneq ($(MODULE), LibINN)
LIBINN=inn
LIBRARY inn $(NS_LIBDIR)/libinn
endif


#ifeq ($(ARCH), WINNT)
#ifneq ($(MODULE), LibNSPR)
#ifeq ($(DEBUG), purify)
#LIBNSPR=$(NSCP_DISTDIR)/lib/$(NSPR_BASENAME).$(LIB_SUFFIX)
#else
#LIBNSPR=$(NSCP_DISTDIR)/lib/$(NSPR_BASENAME).$(LIB_SUFFIX)
#endif
#NSPRDIR=nspr20
#DISTLIB libnspr $(BUILD_ROOT)/$(NSPRDIR)
#endif
#else
#ifneq ($(MODULE), LibNSPR)
#ifeq ($(DEBUG), purify)
#LIBNSPR=$(NSCP_DISTDIR)/lib/purelibnspr.$(LIB_SUFFIX) 
#SHLIBNSPR=$(NSCP_DISTDIR)/lib/purelibnspr$(DLL_PRESUF).$(DLL_SUFFIX) 
#else
#LIBNSPR=$(NSCP_DISTDIR)/lib/$(NSPR_BASENAME).$(LIB_SUFFIX) 
#SHLIBNSPR=$(NSCP_DISTDIR)/lib/$(NSPR_BASENAME)$(DLL_PRESUF).$(DLL_SUFFIX) 
#endif
#NSPRDIR=nspr20
#DISTLIB libnspr $(BUILD_ROOT)/$(NSPRDIR)
#endif
#endif
#
#ifneq ($(MODULE), LibSSLio)
#LIBSSLIO=$(NSCP_DISTDIR)/lib/libsslio.$(LIB_SUFFIX)
#DISTLIB libsslio $(BUILD_ROOT)/$(NSPRDIR)/lib/sslio libsslio
#endif

ifneq ($(MODULE), LibDirMon)
LIBDIRMON=$(NSCP_DISTDIR)/lib/libdirmon.$(LIB_SUFFIX)
#DISTLIB libdirmon $(BUILD_ROOT)/$(NSPRDIR)/lib/dirmon libdirmon
DISTLIB libdirmon $(BUILD_ROOT)/nspr20/lib/dirmon libdirmon
endif


#LibAres and LibPRstrm are from NSPR20 BIN release
#ifneq ($(MODULE), LibAres)
#LIBARES=$(NSCP_DISTDIR)/lib/libares.$(LIB_SUFFIX)
#DISTLIB libares $(BUILD_ROOT)/$(NSPRDIR)/lib/arlib libares
#endif

#ifneq ($(MODULE), LibPRstrm)
#LIBPRSTRMS=$(NSCP_DISTDIR)/lib/libprstrms.$(LIB_SUFFIX)
#DISTLIB libprstrms $(BUILD_ROOT)/$(NSPRDIR)/lib/prstreams libprstrms
#endif

#ifneq ($(MODULE), LibXP)
#LIBXP=$(MCOM_LIBDIR)/xp/$(NSOBJDIR_NAME)/libxp.$(LIB_SUFFIX)
#DISTLIB libxp $(MCOM_LIBDIR)/xp libnspr
#DISTLIB libxp $(MCOM_LIBDIR)/xp 
#endif

#ifneq ($(MODULE), LibDBM)
#LIBDBM=$(MCOM_LIBDIR)/libdbm/$(NSOBJDIR_NAME)/libdbm.$(LIB_SUFFIX)
#DISTLIB libdbm $(MCOM_LIBDIR)/libdbm libnspr
#endif

ifneq ($(MODULE), LibNT)
LIBNT=$(MCOM_LIBDIR)/libnt/$(NSOBJDIR_NAME)/libnt.$(LIB_SUFFIX)
DISTLIB libnt $(MCOM_LIBDIR)/libnt
endif

#ifneq ($(MODULE), LibSecurity)
#ifeq ($(SECURITY), domestic)
#WHICHA=us
#else
#WHICHA=export
#endif
#LIBSEC=$(MCOM_LIBDIR)/libsec/$(NSOBJDIR_NAME)/libsec-$(WHICHA).$(LIB_SUFFIX) $(LIBDBM) $(LIBXP)
#LIBSECNAME=libsec-$(WHICHA)
#libsec: $(LIBSECNAME)
#DISTLIB libsec-$(WHICHA) $(MCOM_LIBDIR)/libsec libnspr libdbm libxp
#endif

ifdef FORTEZZA
ifeq ($(ARCH), WINNT)
LIBSEC += $(MCOM_LIBDIR)/../dist/$(NSOBJDIR_NAME)/lib/tssp32.lib
else
FORTEZZA_DRIVER = $(MCOM_LIBDIR)/../dist/$(NSOBJDIR_NAME)/lib/libci.a
endif
LIBSEC += $(FORTEZZA_DRIVER)
endif

ifneq ($(MODULE), LibNet)
LIBNET=$(MCOM_LIBDIR)/libnet/$(NSOBJDIR_NAME)/libnet.$(LIB_SUFFIX)
DISTLIB libnet.$(LIB_SUFFIX) $(MCOM_LIBDIR)/libnet
endif

ifneq ($(MODULE), LibCS)
LIBCS=libcs
LIBRARY libcs $(NS_LIBDIR)/libcs
endif

ifneq ($(MODULE), LibRobotAPI)
LIBROBOTAPI=librobotapi
LIBRARY librobotapi $(BATMAN)/rds/api
endif

ifneq ($(MODULE), httpdAdminHTML)
MODULE httpd-adm-html $(HTTPD)/newadmin/html
endif

ifneq ($(MODULE), httpdAdminIcons)
MODULE httpd-adm-icons $(HTTPD)/newadmin/icons
endif

ifeq ($(ARCH), WINNT)
ifneq ($(MODULE), httpdAdminBin)
# the admin binaries link with the Server DLL
MODULE httpd-adm-bin $(HTTPD)/newadmin/src
endif
endif
ifneq ($(MODULE), httpdAdminBin)
ifneq ($(ARCH), WINNT)
MODULE httpd-adm-bin $(HTTPD)/newadmin/src
endif
endif

ifneq ($(MODULE), httpdInstall)
MODULE httpd-inst $(HTTPD)/newinst
endif

ifneq ($(MODULE), httpdBinary)
MODULE httpd-bin $(HTTPD)/src
endif

ifneq ($(MODULE), httpdExtrasDatabase)
MODULE httpd-extra-db libxp
endif


ifneq ($(MODULE), httpSubagtBinary)
MODULE http-subagt-bin $(HTTPD)/plugins/snmp
endif


ifneq ($(MODULE), proxyExtras)
MODULE proxy-extra libxp
endif

ifneq ($(MODULE), proxyAdminHTML)
MODULE proxy-adm-html $(PROXY)/newadmin/html
endif

ifneq ($(MODULE), proxyAdminIcons)
MODULE proxy-adm-icons $(PROXY)/newadmin/icons
endif

ifneq ($(MODULE), proxyAdminBin)
MODULE proxy-adm-bin $(PROXY)/newadmin/src
endif

ifneq ($(MODULE), proxyInstallHTML)
MODULE proxy-inst-html $(PROXY)/newinst/html
endif

ifneq ($(MODULE), proxyInstallBin)
MODULE proxy-inst-bin $(PROXY)/newinst/src
endif

ifneq ($(MODULE), proxyBinary)
MODULE proxy-bin $(PROXY)/src
endif

ifneq ($(MODULE), admservBinary)
MODULE admin-server $(BUILD_ROOT)/admserv libnspr frame safs libsnmp libadmin libadminutil libsec-$(WHICHA)
endif

ifneq ($(MODULE), mailServer)
MODULE mail-server $(BUILD_ROOT)/mailserv2/code libnspr
endif

ifneq ($(MODULE), mailAdmin)
MODULE mail-admin $(BUILD_ROOT)/mailserv2/admin libnspr frame libsec-$(WHICHA) admin
endif

ifneq ($(MODULE), mailInstall)
MODULE mail-inst $(BUILD_ROOT)/mailserv2/install libnspr cgiutils regex frame
endif

ifneq ($(MODULE), nnrpdBinary)
MODULE news-nnrpd $(BUILD_ROOT)/news/nnrpd libnspr inn base libsec-$(WHICHA)
endif

ifneq ($(MODULE), inndBinary)
MODULE news-innd $(BUILD_ROOT)/news/innd libnspr inn base libsec-$(WHICHA)
endif

ifneq ($(MODULE), innBackEnds)
MODULE news-backends $(BUILD_ROOT)/news/backends libnspr inn base libsec-$(WHICHA)
endif

ifneq ($(MODULE), innExpire)
MODULE news-expire $(BUILD_ROOT)/news/expire libnspr inn
endif

ifneq ($(MODULE), innFrontEnds)
MODULE news-frontends $(BUILD_ROOT)/news/frontends libnspr inn
endif

ifneq ($(MODULE), innInstall)
MODULE news-install $(BUILD_ROOT)/news/newinst libnspr inn admin base
endif

ifneq ($(MODULE), innAdmin)
MODULE news-admin $(BUILD_ROOT)/news/admin libnspr inn admin base 
endif

ifneq ($(MODULE), innSiteFiles)
MODULE news-site $(BUILD_ROOT)/news/site libnspr inn
endif


ifneq ($(MODULE), batmanDS)
MODULE batman-ds $(BATMAN)/ds libcs
endif

ifneq ($(MODULE), batmanClient)
MODULE batman-client $(BATMAN)/client libcs
endif

ifneq ($(MODULE), batmanRDS)
MODULE batman-rds $(BATMAN)/rds libnspr libcs regex libxp libdbm libnet.$(LIB_SUFFIX) libsec-$(WHICHA) base frame
endif

ifneq ($(MODULE), batmanMiniRDS)
MODULE batman-minirds $(BATMAN)/minirds 
endif

ifneq ($(MODULE), batmanDBA)
MODULE batman-dba $(BATMAN)/dba libcs libdbm
endif

ifneq ($(MODULE), batmanTaxonomy)
MODULE batman-taxonomy $(BATMAN)/tax libcs
endif

ifneq ($(MODULE), httpd-extras)
MODULE httpd-extras $(HTTPD)/extras
endif

ifneq ($(MODULE), httpd-mc-icons)
MODULE httpd-mc-icons $(BUILD_ROOT)/mc-icons
endif

ifneq ($(MODULE), cms-rogue)
MODULE cms-rogue $(ROGUE)
endif

ifneq ($(MODULE), cms-cert)
MODULE cms-cert $(BUILD_ROOT)/certsvc
endif

ifneq ($(MODULE), ns-config)
MODULE ns-config $(BUILD_ROOT)/config
endif

# httpd-bin first so the dll gets built
ifeq ($(ARCH), WINNT)
PACKAGE httpd httpd-adm-bin httpd-adm-html httpd-adm-icons httpd-inst
else
PACKAGE httpd httpd-bin 
endif

PACKAGE proxy proxy-bin proxy-adm-html proxy-adm-bin proxy-adm-icons proxy-inst-html proxy-inst-bin

PACKAGE mail mail-server mail-admin mail-inst

PACKAGE news news-backends news-expire news-frontends news-innd news-install news-admin news-nnrpd news-site

PACKAGE admserv admin-server

PACKAGE batman batman-rds batman-minirds batman-ds batman-client batman-taxonomy


PACKAGE cms-httpd  httpd-adm-bin httpd-adm-html httpd-adm-icons httpd-mc-icons httpd-extras httpd-inst
# base frame admin libaccess cgiutils  
PACKAGE cms-server ns-config libnspr libdbm libsec-$(WHICHA) libxp cms-rogue cms-cert
