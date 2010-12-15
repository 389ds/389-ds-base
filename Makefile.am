# look for included m4 files in the ./m4/ directory
ACLOCAL_AMFLAGS = -I m4
NULLSTRING :=
SPACE := $(NULLSTRING) # the space is between the ) and the #
COLON := $(NULLSTRING):# a colon
QUOTE := $(NULLSTRING)"# a double quote"

#------------------------
# Compiler Flags
#------------------------
BUILDNUM := $(shell perl $(srcdir)/buildnum.pl)
NQBUILDNUM := $(subst \,,$(subst $(QUOTE),,$(BUILDNUM)))
DEBUG_DEFINES = @debug_defs@
# the -U undefines these symbols - should use the corresponding DS_ ones instead - see configure.ac
DS_DEFINES = -DBUILD_NUM=$(BUILDNUM) -DVENDOR="\"$(vendor)\"" -DBRAND="\"$(brand)\"" -DCAPBRAND="\"$(capbrand)\"" \
	-UPACKAGE_VERSION -UPACKAGE_TARNAME -UPACKAGE_STRING -UPACKAGE_BUGREPORT
DS_INCLUDES = -I$(srcdir)/ldap/include -I$(srcdir)/ldap/servers/slapd -I$(srcdir)/include -I.
# these paths are dependent on the settings of prefix and exec_prefix which may be specified
# at make time.  So we cannot use AC_DEFINE in the configure.ac because that would set the
# values prior to their being defined.  Defining them here ensures that they are properly
# expanded before use.  See create_instance.h for more details. The quoting ensures that
# the values are quoted for the shell command, and the value expands to a quoted string
# value in the header file e.g.
# #define LOCALSTATEDIR "/var"
# without the quotes, it would be
# #define LOCALSTATEDIR /var
# which would be an error
PATH_DEFINES = -DLOCALSTATEDIR="\"$(localstatedir)\"" -DSYSCONFDIR="\"$(sysconfdir)\"" \
	-DLIBDIR="\"$(libdir)\"" -DBINDIR="\"$(bindir)\"" \
	-DDATADIR="\"$(datadir)\"" -DDOCDIR="\"$(docdir)\"" \
	-DSBINDIR="\"$(sbindir)\"" -DPLUGINDIR="\"$(serverplugindir)\"" -DTEMPLATEDIR="\"$(sampledatadir)\""

AM_CPPFLAGS = $(DEBUG_DEFINES) $(DS_DEFINES) $(DS_INCLUDES) $(PATH_DEFINES)
PLUGIN_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
# We need to make sure that libpthread is linked before libc on HP-UX.
if HPUX
AM_LDFLAGS = -lpthread
#else
#AM_LDFLAGS = -Wl,-z,defs
endif

#------------------------
# Linker Flags
#------------------------
NSPR_LINK = @nspr_lib@ -lplc4 -lplds4 -lnspr4
NSS_LINK = @nss_lib@ -lssl3 -lnss3
if OPENLDAP
LDAPSDK_LINK = @openldap_lib@ -lldap_r@ol_libver@ -lldap@ol_libver@ -lldif@ol_libver@ -llber@ol_libver@
else
LDAPSDK_LINK = @ldapsdk_lib@ -lssldap60 -lprldap60 -lldap60 -lldif60
endif
DB_LINK = @db_lib@ -ldb-@db_libver@
SASL_LINK = @sasl_lib@ -lsasl2
SVRCORE_LINK = @svrcore_lib@ -lsvrcore
ICU_LINK = @icu_lib@ -licui18n -licuuc -licudata
PCRE_LINK = @pcre_lib@ -lpcre
NETSNMP_LINK = @netsnmp_lib@ @netsnmp_link@
PAM_LINK = -lpam
KERBEROS_LINK = $(kerberos_lib)

LIBSOCKET=@LIBSOCKET@
LIBNSL=@LIBNSL@
LIBDL=@LIBDL@
LIBCSTD=@LIBCSTD@
LIBCRUN=@LIBCRUN@
THREADLIB=@THREADLIB@
LIBCRYPT=@LIBCRYPT@

#------------------------
# Generated Sources
#------------------------
BUILT_SOURCES = dberrstrs.h \
	$(POLICY_FC)

CLEANFILES =  dberrstrs.h ns-slapd.properties \
	ldap/admin/src/scripts/template-dbverify ldap/admin/src/template-initconfig \
	ldap/admin/src/scripts/dscreate.map ldap/admin/src/scripts/remove-ds.pl \
	ldap/admin/src/scripts/DSCreate.pm ldap/admin/src/scripts/DSMigration.pm \
	ldap/admin/src/scripts/DSUpdate.pm ldap/admin/src/scripts/dsupdate.map \
	ldap/admin/src/scripts/dsorgentries.map ldap/admin/src/scripts/migrate-ds.pl \
	ldap/admin/src/scripts/Migration.pm ldap/admin/src/scripts/SetupDialogs.pm \
	ldap/admin/src/scripts/setup-ds.pl ldap/admin/src/scripts/setup-ds.res \
	ldap/admin/src/scripts/start-dirsrv ldap/admin/src/scripts/stop-dirsrv \
	ldap/admin/src/scripts/restart-dirsrv ldap/admin/src/scripts/Setup.pm \
	ldap/admin/src/scripts/template-bak2db ldap/admin/src/scripts/template-bak2db.pl \
	ldap/admin/src/scripts/template-db2bak ldap/admin/src/scripts/template-db2bak.pl \
	ldap/admin/src/scripts/template-db2index ldap/admin/src/scripts/template-db2index.pl \
	ldap/admin/src/scripts/template-db2ldif ldap/admin/src/scripts/template-db2ldif.pl \
	ldap/admin/src/scripts/template-ldif2db ldap/admin/src/scripts/template-ldif2db.pl \
	ldap/admin/src/scripts/template-ldif2ldap ldap/admin/src/scripts/template-monitor \
	ldap/admin/src/scripts/template-ns-accountstatus.pl ldap/admin/src/scripts/template-ns-activate.pl \
	ldap/admin/src/scripts/template-ns-inactivate.pl ldap/admin/src/scripts/template-ns-newpwpolicy.pl \
	ldap/admin/src/scripts/template-restart-slapd ldap/admin/src/scripts/template-restoreconfig \
	ldap/admin/src/scripts/template-saveconfig ldap/admin/src/scripts/template-start-slapd \
	ldap/admin/src/scripts/template-stop-slapd ldap/admin/src/scripts/template-suffix2instance \
	ldap/admin/src/scripts/template-upgradedb \
	ldap/admin/src/scripts/template-upgradednformat \
	ldap/admin/src/scripts/template-usn-tombstone-cleanup.pl \
	ldap/admin/src/scripts/template-verify-db.pl \
	ldap/admin/src/scripts/template-vlvindex ldap/admin/src/scripts/DSUtil.pm \
	ldap/ldif/template-baseacis.ldif ldap/ldif/template-bitwise.ldif ldap/ldif/template-country.ldif \
	ldap/ldif/template-dnaplugin.ldif ldap/ldif/template-domain.ldif ldap/ldif/template-dse.ldif \
	ldap/ldif/50replication-plugins.ldif ldap/ldif/template-ldapi-autobind.ldif ldap/ldif/template-ldapi-default.ldif \
	ldap/ldif/template-ldapi.ldif ldap/ldif/template-locality.ldif ldap/ldif/template-org.ldif \
	ldap/ldif/template-orgunit.ldif ldap/ldif/template-pampta.ldif ldap/ldif/template-sasl.ldif \
	ldap/ldif/template-state.ldif ldap/ldif/template-suffix-db.ldif

clean-local:
	-rm -rf selinux-built

dberrstrs.h: Makefile
	perl $(srcdir)/ldap/servers/slapd/mkDBErrStrs.pl -i @db_incdir@ -o .

selinux-built:
	cp -r $(srcdir)/selinux $@

selinux-built/dirsrv.fc: selinux-built
	$(fixupcmd) selinux-built/dirsrv.fc.in > $@


#------------------------
# Install Paths
#------------------------
configdir = $(sysconfdir)@configdir@
sampledatadir = $(datadir)@sampledatadir@
propertydir = $(datadir)@propertydir@
schemadir = $(sysconfdir)@schemadir@
serverdir = $(libdir)@serverdir@
serverplugindir = $(libdir)@serverplugindir@
taskdir = $(datadir)@scripttemplatedir@
initdir = @initdir@
initconfigdir = $(sysconfdir)@initconfigdir@
instconfigdir = @instconfigdir@
perldir = $(libdir)@perldir@
infdir = $(datadir)@infdir@
mibdir = $(datadir)@mibdir@
updatedir = $(datadir)@updatedir@

defaultuser=@defaultuser@
defaultgroup=@defaultgroup@

#------------------------
# Build Products
#------------------------
sbin_PROGRAMS = ns-slapd ldap-agent-bin

bin_PROGRAMS = dbscan-bin dsktune-bin infadd-bin ldclt-bin \
	ldif-bin migratecred-bin mmldif-bin pwdhash-bin rsearch-bin

server_LTLIBRARIES = libslapd.la libns-dshttpd.la

# this is how to add optional plugins
if enable_pam_passthru
LIBPAM_PASSTHRU_PLUGIN = libpam-passthru-plugin.la
enable_pam_passthru = 1
endif
if enable_dna
LIBDNA_PLUGIN = libdna-plugin.la
enable_dna = 1
endif

if enable_bitwise
LIBBITWISE_PLUGIN = libbitwise-plugin.la
enable_bitwise = 1
endif

if enable_presence
LIBPRESENCE_PLUGIN = libpresence-plugin.la
LIBPRESENCE_SCHEMA = $(srcdir)/ldap/schema/10presence.ldif
enable_presence = on
else
enable_presence = off
endif

if SELINUX
POLICY_FC = selinux-built/dirsrv.fc
endif

if enable_acctpolicy
LIBACCTPOLICY_PLUGIN = libacctpolicy-plugin.la
LIBACCTPOLICY_SCHEMA = $(srcdir)/ldap/schema/60acctpolicy.ldif
enable_acctpolicy = 1
endif

serverplugin_LTLIBRARIES = libacl-plugin.la libattr-unique-plugin.la \
	libback-ldbm.la libchainingdb-plugin.la libcollation-plugin.la \
	libcos-plugin.la libderef-plugin.la libdes-plugin.la libdistrib-plugin.la \
	libhttp-client-plugin.la liblinkedattrs-plugin.la libmanagedentries-plugin.la \
	libmemberof-plugin.la libpassthru-plugin.la libpwdstorage-plugin.la \
	libreferint-plugin.la libreplication-plugin.la libretrocl-plugin.la \
	libroles-plugin.la libstatechange-plugin.la libsyntax-plugin.la \
	libviews-plugin.la libschemareload-plugin.la libusn-plugin.la \
	$(LIBACCTPOLICY_PLUGIN) \
	$(LIBPAM_PASSTHRU_PLUGIN) $(LIBDNA_PLUGIN) \
	$(LIBBITWISE_PLUGIN) $(LIBPRESENCE_PLUGIN)

nodist_property_DATA = ns-slapd.properties

noinst_PROGRAMS = makstrdb

noinst_LIBRARIES = libavl.a libldaputil.a

#------------------------
# Installed Files
#------------------------
config_DATA = $(srcdir)/lib/ldaputil/certmap.conf \
	$(srcdir)/ldap/schema/slapd-collations.conf \
	ldap/admin/src/template-initconfig \
	ldap/servers/snmp/ldap-agent.conf

# the schema files in this list are either not
# standard schema, not tested, or not compatible
# with the default schema e.g. there is
# considerable overlap of 60changelog.ldif and 01common.ldif
# and 60inetmail.ldif and 50ns-mail.ldif among others
sampledata_DATA = ldap/admin/src/scripts/failedbinds.py \
	ldap/admin/src/scripts/logregex.py \
	$(srcdir)/ldap/ldif/Ace.ldif \
	$(srcdir)/ldap/ldif/European.ldif \
	$(srcdir)/ldap/ldif/Eurosuffix.ldif \
	$(srcdir)/ldap/ldif/Example.ldif \
	$(srcdir)/ldap/ldif/Example-roles.ldif \
	$(srcdir)/ldap/ldif/Example-views.ldif \
	$(srcdir)/ldap/ldif/template.ldif \
	ldap/ldif/template-dse.ldif \
	ldap/ldif/template-suffix-db.ldif \
	ldap/ldif/template-ldapi.ldif \
	ldap/ldif/template-ldapi-default.ldif \
	ldap/ldif/template-ldapi-autobind.ldif \
	ldap/ldif/template-pampta.ldif \
	ldap/ldif/template-dnaplugin.ldif \
	ldap/ldif/template-bitwise.ldif \
	ldap/ldif/template-org.ldif \
	ldap/ldif/template-domain.ldif \
	ldap/ldif/template-state.ldif \
	ldap/ldif/template-locality.ldif \
	ldap/ldif/template-country.ldif \
	ldap/ldif/template-orgunit.ldif \
	ldap/ldif/template-baseacis.ldif \
	ldap/ldif/template-sasl.ldif \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-FamilyNames \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-GivenNames \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-OrgUnits \
	$(srcdir)/ldap/schema/10rfc2307bis.ldif \
	$(srcdir)/ldap/schema/60changelog.ldif \
	$(srcdir)/ldap/schema/60inetmail.ldif \
	$(srcdir)/ldap/schema/60krb5kdc.ldif \
	$(srcdir)/ldap/schema/60kerberos.ldif \
	$(srcdir)/ldap/schema/60nis.ldif \
	$(srcdir)/ldap/schema/60qmail.ldif \
	$(srcdir)/ldap/schema/60radius.ldif \
	$(srcdir)/ldap/schema/60rfc4876.ldif \
	$(srcdir)/ldap/schema/60samba.ldif \
	$(srcdir)/ldap/schema/60samba3.ldif \
	$(LIBPRESENCE_SCHEMA)

schema_DATA = $(srcdir)/ldap/schema/00core.ldif \
	$(srcdir)/ldap/schema/01core389.ldif \
	$(srcdir)/ldap/schema/02common.ldif \
	$(srcdir)/ldap/schema/05rfc2927.ldif \
	$(srcdir)/ldap/schema/05rfc4523.ldif \
	$(srcdir)/ldap/schema/05rfc4524.ldif \
	$(srcdir)/ldap/schema/06inetorgperson.ldif \
	$(srcdir)/ldap/schema/10mep-plugin.ldif \
	$(srcdir)/ldap/schema/10rfc2307.ldif \
	$(srcdir)/ldap/schema/20subscriber.ldif \
	$(srcdir)/ldap/schema/25java-object.ldif \
	$(srcdir)/ldap/schema/28pilot.ldif \
	$(srcdir)/ldap/schema/30ns-common.ldif \
	$(srcdir)/ldap/schema/50ns-admin.ldif \
	$(srcdir)/ldap/schema/50ns-certificate.ldif \
	$(srcdir)/ldap/schema/50ns-directory.ldif \
	$(srcdir)/ldap/schema/50ns-mail.ldif \
	$(srcdir)/ldap/schema/50ns-value.ldif \
	$(srcdir)/ldap/schema/50ns-web.ldif \
	$(srcdir)/ldap/schema/60pam-plugin.ldif \
	$(srcdir)/ldap/schema/60autofs.ldif \
	$(srcdir)/ldap/schema/60eduperson.ldif \
	$(srcdir)/ldap/schema/60mozilla.ldif \
	$(srcdir)/ldap/schema/60pureftpd.ldif \
	$(srcdir)/ldap/schema/60rfc2739.ldif \
	$(srcdir)/ldap/schema/60rfc3712.ldif \
	$(srcdir)/ldap/schema/60sabayon.ldif \
	$(srcdir)/ldap/schema/60sudo.ldif \
	$(srcdir)/ldap/schema/60trust.ldif \
	$(srcdir)/ldap/schema/60nss-ldap.ldif \
	$(srcdir)/ldap/schema/99user.ldif \
	$(LIBACCTPOLICY_SCHEMA)

sbin_SCRIPTS = ldap/admin/src/scripts/setup-ds.pl \
	ldap/admin/src/scripts/migrate-ds.pl \
	ldap/admin/src/scripts/remove-ds.pl \
	ldap/admin/src/scripts/start-dirsrv \
	ldap/admin/src/scripts/stop-dirsrv \
	ldap/admin/src/scripts/restart-dirsrv \
        wrappers/ldap-agent

bin_SCRIPTS = ldap/servers/slapd/tools/rsearch/scripts/dbgen.pl \
	wrappers/dbscan \
	wrappers/dsktune \
	wrappers/infadd \
	wrappers/ldclt \
	wrappers/ldif \
	$(srcdir)/ldap/admin/src/logconv.pl \
	wrappers/migratecred \
	wrappers/mmldif \
	wrappers/pwdhash \
	wrappers/rsearch \
	wrappers/cl-dump \
	ldap/admin/src/scripts/cl-dump.pl \
	wrappers/repl-monitor \
	ldap/admin/src/scripts/repl-monitor.pl \
	ldap/admin/src/scripts/ds-logpipe.py

# SCRIPTS makes them executables - these are perl modules
# and should not be marked as executable - so use DATA
perl_DATA = ldap/admin/src/scripts/SetupLog.pm \
	ldap/admin/src/scripts/Resource.pm \
	ldap/admin/src/scripts/DSUtil.pm \
	ldap/admin/src/scripts/Setup.pm \
	ldap/admin/src/scripts/SetupDialogs.pm \
	ldap/admin/src/scripts/Inf.pm \
	ldap/admin/src/scripts/DialogManager.pm \
	ldap/admin/src/scripts/Dialog.pm \
	ldap/admin/src/scripts/DSDialogs.pm \
	ldap/admin/src/scripts/Migration.pm \
	ldap/admin/src/scripts/DSMigration.pm \
	ldap/admin/src/scripts/FileConn.pm \
	ldap/admin/src/scripts/DSCreate.pm \
	ldap/admin/src/scripts/DSUpdate.pm \
	ldap/admin/src/scripts/DSUpdateDialogs.pm

property_DATA = ldap/admin/src/scripts/setup-ds.res \
	ldap/admin/src/scripts/migrate-ds.res

task_SCRIPTS = ldap/admin/src/scripts/template-bak2db \
	ldap/admin/src/scripts/template-db2bak \
	ldap/admin/src/scripts/template-db2index \
	ldap/admin/src/scripts/template-db2ldif \
	ldap/admin/src/scripts/template-dn2rdn \
	ldap/admin/src/scripts/template-ldif2db \
	ldap/admin/src/scripts/template-ldif2ldap \
	ldap/admin/src/scripts/template-monitor \
	ldap/admin/src/scripts/template-restart-slapd \
	ldap/admin/src/scripts/template-restoreconfig \
	ldap/admin/src/scripts/template-saveconfig \
	ldap/admin/src/scripts/template-start-slapd \
	ldap/admin/src/scripts/template-stop-slapd \
	ldap/admin/src/scripts/template-suffix2instance \
	ldap/admin/src/scripts/template-upgradednformat \
	ldap/admin/src/scripts/template-vlvindex \
	ldap/admin/src/scripts/template-bak2db.pl \
	ldap/admin/src/scripts/template-db2bak.pl \
	ldap/admin/src/scripts/template-db2index.pl \
	ldap/admin/src/scripts/template-db2ldif.pl \
	ldap/admin/src/scripts/template-fixup-linkedattrs.pl \
	ldap/admin/src/scripts/template-fixup-memberof.pl \
	ldap/admin/src/scripts/template-ldif2db.pl \
	ldap/admin/src/scripts/template-ns-accountstatus.pl \
	ldap/admin/src/scripts/template-ns-activate.pl \
	ldap/admin/src/scripts/template-ns-inactivate.pl \
	ldap/admin/src/scripts/template-ns-newpwpolicy.pl \
	ldap/admin/src/scripts/template-schema-reload.pl \
	ldap/admin/src/scripts/template-syntax-validate.pl \
	ldap/admin/src/scripts/template-usn-tombstone-cleanup.pl \
	ldap/admin/src/scripts/template-verify-db.pl \
	ldap/admin/src/scripts/template-dbverify

init_SCRIPTS = wrappers/$(PACKAGE_NAME) \
	wrappers/$(PACKAGE_NAME)-snmp

initconfig_DATA = ldap/admin/src/$(PACKAGE_NAME)

inf_DATA = ldap/admin/src/slapd.inf \
	ldap/admin/src/scripts/dscreate.map \
	ldap/admin/src/scripts/dsupdate.map \
	ldap/admin/src/scripts/dsorgentries.map

mib_DATA = ldap/servers/snmp/RFC-1215.txt \
	ldap/servers/snmp/SNMPv2-TC.txt \
	ldap/servers/snmp/redhat-directory.mib \
	ldap/servers/snmp/SNMPv2-CONF.txt \
	ldap/servers/snmp/NETWORK-SERVICES-MIB.txt \
	ldap/servers/snmp/RFC1155-SMI.txt \
	ldap/servers/snmp/SNMPv2-SMI.txt

#------------------------
# man pages
#------------------------
dist_man_MANS = man/man1/dbscan.1 \
        man/man1/cl-dump.1 \
        man/man1/dbgen.pl.1 \
        man/man1/ds-logpipe.py.1 \
        man/man1/dsktune.1 \
        man/man1/infadd.1 \
        man/man1/ldap-agent.1 \
        man/man1/ldclt.1 \
        man/man1/ldif.1 \
        man/man1/logconv.pl.1 \
        man/man1/migratecred.1 \
        man/man1/mmldif.1 \
        man/man1/pwdhash.1 \
        man/man1/repl-monitor.1 \
        man/man1/rsearch.1 \
        man/man8/migrate-ds.pl.8 \
        man/man8/ns-slapd.8 \
        man/man8/restart-dirsrv.8 \
        man/man8/setup-ds.pl.8 \
        man/man8/start-dirsrv.8 \
        man/man8/stop-dirsrv.8 \
	man/man8/remove-ds.pl.8

#------------------------
# updates
# the first 3 are just the examples provided - since they
# do not begin with two digits, they will be ignored
# the remaining items should begin with two digits that
# correspond to the order in which they should be applied
# perl files and LDIF files are DATA - not executable
# processed by the update script
# shell scripts and other files are SCRIPTS - executable
#------------------------
update_DATA = ldap/admin/src/scripts/exampleupdate.pl \
	ldap/admin/src/scripts/exampleupdate.ldif \
	ldap/admin/src/scripts/10cleanupldapi.pl \
	ldap/admin/src/scripts/10delautodnsuffix.pl \
	ldap/admin/src/scripts/10fixrundir.pl \
	ldap/admin/src/scripts/50addchainingsaslpwroles.ldif \
	ldap/admin/src/scripts/50memberofindex.ldif \
	ldap/admin/src/scripts/50bitstringsyntaxplugin.ldif \
	ldap/admin/src/scripts/50memberofplugin.ldif \
	ldap/admin/src/scripts/50deliverymethodsyntaxplugin.ldif \
	ldap/admin/src/scripts/50nameuidsyntaxplugin.ldif \
	ldap/admin/src/scripts/50derefplugin.ldif \
	ldap/admin/src/scripts/50numericstringsyntaxplugin.ldif \
	ldap/admin/src/scripts/50disableurisyntaxplugin.ldif \
	ldap/admin/src/scripts/50printablestringsyntaxplugin.ldif \
	ldap/admin/src/scripts/50enhancedguidesyntaxplugin.ldif \
	ldap/admin/src/scripts/50schemareloadplugin.ldif \
	ldap/admin/src/scripts/50entryusnindex.ldif \
	ldap/admin/src/scripts/50syntaxvalidplugin.ldif \
	ldap/admin/src/scripts/50faxnumbersyntaxplugin.ldif \
	ldap/admin/src/scripts/50teletexterminalidsyntaxplugin.ldif \
	ldap/admin/src/scripts/50faxsyntaxplugin.ldif \
	ldap/admin/src/scripts/50telexnumbersyntaxplugin.ldif \
	ldap/admin/src/scripts/50guidesyntaxplugin.ldif \
	ldap/ldif/50replication-plugins.ldif \
	ldap/admin/src/scripts/50linkedattrsplugin.ldif \
	ldap/admin/src/scripts/50usnplugin.ldif \
	ldap/admin/src/scripts/50smd5pwdstorageplugin.ldif \
	ldap/admin/src/scripts/50retroclprecedence.ldif \
	ldap/admin/src/scripts/60upgradeschemafiles.pl \
	ldap/admin/src/scripts/90subtreerename.pl \
	ldap/admin/src/scripts/80upgradednformat.pl \
	ldap/admin/src/scripts/81changelog.pl \
	ldap/admin/src/scripts/dnaplugindepends.ldif

update_SCRIPTS = ldap/admin/src/scripts/exampleupdate.sh

#////////////////////////////////////////////////////////////////
#
#   Server Strings
#
#////////////////////////////////////////////////////////////////
#------------------------
# makstrdb
#------------------------
makstrdb_SOURCES = lib/libsi18n/makstrdb.c

makstrdb_CPPFLAGS = $(AM_CPPFLAGS) @nspr_inc@

#------------------------
# ns-slapd.properties
#------------------------
ns-slapd.properties: makstrdb
	./makstrdb


#////////////////////////////////////////////////////////////////
#
#   Static Server Libraries
#
#////////////////////////////////////////////////////////////////
#------------------------
# libavl
#------------------------
libavl_a_SOURCES = ldap/libraries/libavl/avl.c

#------------------------
# libldaputil
#------------------------
libldaputil_a_SOURCES = lib/ldaputil/cert.c \
	lib/ldaputil/certmap.c \
	lib/ldaputil/dbconf.c \
	lib/ldaputil/encode.c \
	lib/ldaputil/errors.c \
	lib/ldaputil/init.c \
	lib/ldaputil/ldapauth.c \
	lib/ldaputil/vtable.c

libldaputil_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/lib/ldaputil @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@


#////////////////////////////////////////////////////////////////
#
#   Dynamic Server Libraries
#
#////////////////////////////////////////////////////////////////

#------------------------
# libns-dshttpd
#------------------------
libns_dshttpd_la_SOURCES = lib/libaccess/access_plhash.cpp \
	lib/libaccess/acl.tab.cpp \
	lib/libaccess/acl.yy.cpp \
	lib/libaccess/aclcache.cpp \
	lib/libaccess/aclerror.cpp \
	lib/libaccess/acleval.cpp \
	lib/libaccess/aclflush.cpp \
	lib/libaccess/aclspace.cpp \
	lib/libaccess/acltools.cpp \
	lib/libaccess/aclutil.cpp \
	lib/libaccess/authdb.cpp \
	lib/libaccess/lasdns.cpp \
	lib/libaccess/lasgroup.cpp \
	lib/libaccess/lasip.cpp \
	lib/libaccess/lastod.cpp \
	lib/libaccess/lasuser.cpp \
	lib/libaccess/method.cpp \
	lib/libaccess/nseframe.cpp \
	lib/libaccess/nsautherr.cpp \
	lib/libaccess/oneeval.cpp \
	lib/libaccess/register.cpp \
	lib/libaccess/symbols.cpp \
	lib/libaccess/usi.cpp \
	lib/libaccess/usrcache.cpp \
	lib/libadmin/error.c \
	lib/libadmin/template.c \
	lib/libadmin/util.c \
	lib/base/crit.cpp \
	lib/base/dns.cpp \
	lib/base/dnsdmain.cpp \
	lib/base/ereport.cpp \
	lib/base/file.cpp \
	lib/base/fsmutex.cpp \
	lib/base/net.cpp \
	lib/base/nscperror.c \
	lib/base/plist.cpp \
	lib/base/pool.cpp \
	lib/base/rwlock.cpp \
	lib/base/shexp.cpp \
	lib/base/system.cpp \
	lib/base/systhr.cpp \
	lib/base/util.cpp \
	lib/libsi18n/getstrprop.c \
	lib/libsi18n/reshash.c \
	lib/libsi18n/txtfile.c \
	$(libldaputil_a_SOURCES)

libns_dshttpd_la_CPPFLAGS = -I$(srcdir)/include/base $(AM_CPPFLAGS) -I$(srcdir)/lib/ldaputil @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
libns_dshttpd_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK)

#------------------------
# libslapd
#------------------------
libslapd_la_SOURCES = ldap/servers/slapd/add.c \
	ldap/servers/slapd/agtmmap.c \
	ldap/servers/slapd/apibroker.c \
	ldap/servers/slapd/attr.c \
	ldap/servers/slapd/attrlist.c \
	ldap/servers/slapd/attrsyntax.c \
	ldap/servers/slapd/auditlog.c \
	ldap/servers/slapd/ava.c \
	ldap/servers/slapd/backend.c \
	ldap/servers/slapd/backend_manager.c \
	ldap/servers/slapd/bitset.c \
	ldap/servers/slapd/bulk_import.c \
	ldap/servers/slapd/charray.c \
	ldap/servers/slapd/ch_malloc.c \
	ldap/servers/slapd/computed.c \
	ldap/servers/slapd/control.c \
	ldap/servers/slapd/counters.c \
	ldap/servers/slapd/csn.c \
	ldap/servers/slapd/csngen.c \
	ldap/servers/slapd/csnset.c \
	ldap/servers/slapd/defbackend.c \
	ldap/servers/slapd/delete.c \
	ldap/servers/slapd/dl.c \
	ldap/servers/slapd/dn.c \
	ldap/servers/slapd/dse.c \
	ldap/servers/slapd/dynalib.c \
	ldap/servers/slapd/entry.c \
	ldap/servers/slapd/entrywsi.c \
	ldap/servers/slapd/errormap.c \
	ldap/servers/slapd/eventq.c \
	ldap/servers/slapd/factory.c \
	ldap/servers/slapd/fileio.c \
	ldap/servers/slapd/filter.c \
	ldap/servers/slapd/filtercmp.c \
	ldap/servers/slapd/filterentry.c \
	ldap/servers/slapd/generation.c \
	ldap/servers/slapd/getfilelist.c \
	ldap/servers/slapd/index_subsystem.c \
	ldap/servers/slapd/ldaputil.c \
	ldap/servers/slapd/lenstr.c \
	ldap/servers/slapd/libglobs.c \
	ldap/servers/slapd/localhost.c \
	ldap/servers/slapd/log.c \
	ldap/servers/slapd/mapping_tree.c \
	ldap/servers/slapd/match.c \
	ldap/servers/slapd/modify.c \
	ldap/servers/slapd/modrdn.c \
	ldap/servers/slapd/modutil.c \
	ldap/servers/slapd/ntuserpin.c \
	ldap/servers/slapd/object.c \
	ldap/servers/slapd/objset.c \
	ldap/servers/slapd/operation.c \
	ldap/servers/slapd/opshared.c \
	ldap/servers/slapd/pagedresults.c \
	ldap/servers/slapd/pblock.c \
	ldap/servers/slapd/plugin.c \
	ldap/servers/slapd/plugin_acl.c \
	ldap/servers/slapd/plugin_internal_op.c \
	ldap/servers/slapd/plugin_mr.c \
	ldap/servers/slapd/plugin_role.c \
	ldap/servers/slapd/plugin_syntax.c \
	ldap/servers/slapd/proxyauth.c \
	ldap/servers/slapd/pw.c \
	ldap/servers/slapd/pw_retry.c \
	ldap/servers/slapd/rdn.c \
	ldap/servers/slapd/referral.c \
	ldap/servers/slapd/regex.c \
	ldap/servers/slapd/resourcelimit.c \
	ldap/servers/slapd/result.c \
	ldap/servers/slapd/rwlock.c \
	ldap/servers/slapd/sasl_map.c \
	ldap/servers/slapd/schema.c \
	ldap/servers/slapd/schemaparse.c \
	ldap/servers/slapd/security_wrappers.c \
	ldap/servers/slapd/slapd_plhash.c \
	ldap/servers/slapd/slapi_counter.c \
	ldap/servers/slapd/slapi2nspr.c \
	ldap/servers/slapd/snmp_collator.c \
	ldap/servers/slapd/sort.c \
	ldap/servers/slapd/ssl.c \
	ldap/servers/slapd/str2filter.c \
	ldap/servers/slapd/subentry.c \
	ldap/servers/slapd/task.c \
	ldap/servers/slapd/time.c \
	ldap/servers/slapd/uniqueid.c \
	ldap/servers/slapd/uniqueidgen.c \
	ldap/servers/slapd/utf8.c \
	ldap/servers/slapd/utf8compare.c \
	ldap/servers/slapd/util.c \
	ldap/servers/slapd/uuid.c \
	ldap/servers/slapd/value.c \
	ldap/servers/slapd/valueset.c \
	ldap/servers/slapd/vattr.c \
	$(libavl_a_SOURCES)

libslapd_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @sasl_inc@ @db_inc@ @svrcore_inc@ @kerberos_inc@ @pcre_inc@
if SOLARIS
libslapd_la_SOURCES += ldap/servers/slapd/slapi_counter_sunos_sparcv9.S
endif

libslapd_la_LIBADD = $(LDAPSDK_LINK) $(SASL_LINK) $(SVRCORE_LINK) $(NSS_LINK) $(NSPR_LINK) $(KERBEROS_LINK) $(PCRE_LINK) $(THREADLIB)


#////////////////////////////////////////////////////////////////
#
#   Plugins
#
#////////////////////////////////////////////////////////////////
#------------------------
# libback-ldbm
#------------------------
libback_ldbm_la_SOURCES = ldap/servers/slapd/back-ldbm/ancestorid.c \
	ldap/servers/slapd/back-ldbm/archive.c \
	ldap/servers/slapd/back-ldbm/backentry.c \
	ldap/servers/slapd/back-ldbm/cache.c \
	ldap/servers/slapd/back-ldbm/cleanup.c \
	ldap/servers/slapd/back-ldbm/close.c \
	ldap/servers/slapd/back-ldbm/dbhelp.c \
	ldap/servers/slapd/back-ldbm/dblayer.c \
	ldap/servers/slapd/back-ldbm/dbsize.c \
	ldap/servers/slapd/back-ldbm/dbversion.c \
	ldap/servers/slapd/back-ldbm/dn2entry.c \
	ldap/servers/slapd/back-ldbm/entrystore.c \
	ldap/servers/slapd/back-ldbm/filterindex.c \
	ldap/servers/slapd/back-ldbm/findentry.c \
	ldap/servers/slapd/back-ldbm/haschildren.c \
	ldap/servers/slapd/back-ldbm/id2entry.c \
	ldap/servers/slapd/back-ldbm/idl.c \
	ldap/servers/slapd/back-ldbm/idl_shim.c \
	ldap/servers/slapd/back-ldbm/idl_new.c \
	ldap/servers/slapd/back-ldbm/idl_common.c \
	ldap/servers/slapd/back-ldbm/import.c \
	ldap/servers/slapd/back-ldbm/import-merge.c \
	ldap/servers/slapd/back-ldbm/import-threads.c \
	ldap/servers/slapd/back-ldbm/index.c \
	ldap/servers/slapd/back-ldbm/init.c \
	ldap/servers/slapd/back-ldbm/instance.c \
	ldap/servers/slapd/back-ldbm/ldbm_abandon.c \
	ldap/servers/slapd/back-ldbm/ldbm_add.c \
	ldap/servers/slapd/back-ldbm/ldbm_attr.c \
	ldap/servers/slapd/back-ldbm/ldbm_attrcrypt.c \
	ldap/servers/slapd/back-ldbm/ldbm_attrcrypt_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_bind.c \
	ldap/servers/slapd/back-ldbm/ldbm_compare.c \
	ldap/servers/slapd/back-ldbm/ldbm_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_delete.c \
	ldap/servers/slapd/back-ldbm/ldbm_entryrdn.c \
	ldap/servers/slapd/back-ldbm/ldbm_index_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_instance_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_modify.c \
	ldap/servers/slapd/back-ldbm/ldbm_modrdn.c \
	ldap/servers/slapd/back-ldbm/ldbm_search.c \
	ldap/servers/slapd/back-ldbm/ldbm_unbind.c \
	ldap/servers/slapd/back-ldbm/ldbm_usn.c \
	ldap/servers/slapd/back-ldbm/ldif2ldbm.c \
	ldap/servers/slapd/back-ldbm/dbverify.c \
	ldap/servers/slapd/back-ldbm/matchrule.c \
	ldap/servers/slapd/back-ldbm/misc.c \
	ldap/servers/slapd/back-ldbm/monitor.c \
	ldap/servers/slapd/back-ldbm/nextid.c \
	ldap/servers/slapd/back-ldbm/parents.c \
	ldap/servers/slapd/back-ldbm/perfctrs.c \
	ldap/servers/slapd/back-ldbm/rmdb.c \
	ldap/servers/slapd/back-ldbm/seq.c \
	ldap/servers/slapd/back-ldbm/sort.c \
	ldap/servers/slapd/back-ldbm/start.c \
	ldap/servers/slapd/back-ldbm/uniqueid2entry.c \
	ldap/servers/slapd/back-ldbm/upgrade.c \
	ldap/servers/slapd/back-ldbm/vlv.c \
	ldap/servers/slapd/back-ldbm/vlv_key.c \
	ldap/servers/slapd/back-ldbm/vlv_srch.c

libback_ldbm_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @db_inc@
libback_ldbm_la_LIBADD = libslapd.la $(DB_LINK) $(LDAPSDK_LINK) $(NSPR_LINK)
libback_ldbm_la_LDFLAGS = -avoid-version

#------------------------
# libacctpolicy-plugin
#------------------------
libacctpolicy_plugin_la_SOURCES = ldap/servers/plugins/acctpolicy/acct_config.c \
	ldap/servers/plugins/acctpolicy/acct_init.c \
	ldap/servers/plugins/acctpolicy/acct_plugin.c \
	ldap/servers/plugins/acctpolicy/acct_util.c

libacctpolicy_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libacctpolicy_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libacctpolicy_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libacl-plugin
#------------------------
libacl_plugin_la_SOURCES = ldap/servers/plugins/acl/acl.c \
	ldap/servers/plugins/acl/acl_ext.c \
	ldap/servers/plugins/acl/aclanom.c \
	ldap/servers/plugins/acl/acleffectiverights.c \
	ldap/servers/plugins/acl/aclgroup.c \
	ldap/servers/plugins/acl/aclinit.c \
	ldap/servers/plugins/acl/acllas.c \
	ldap/servers/plugins/acl/acllist.c \
	ldap/servers/plugins/acl/aclparse.c \
	ldap/servers/plugins/acl/aclplugin.c \
	ldap/servers/plugins/acl/aclutil.c

libacl_plugin_la_CPPFLAGS = -I$(srcdir)/include/libaccess $(PLUGIN_CPPFLAGS)
libacl_plugin_la_LIBADD = libslapd.la libns-dshttpd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(LIBCSTD) $(LIBCRUN)
libacl_plugin_la_LDFLAGS = -avoid-version
libacl_plugin_la_LINK = $(CXXLINK) -avoid-version

#------------------------
# libattr-unique-plugin
#------------------------
libattr_unique_plugin_la_SOURCES = ldap/servers/plugins/uiduniq/7bit.c \
	ldap/servers/plugins/uiduniq/uid.c \
	ldap/servers/plugins/uiduniq/utils.c

libattr_unique_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libattr_unique_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libattr_unique_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libbitwise-plugin
#------------------------
libbitwise_plugin_la_SOURCES = ldap/servers/plugins/bitwise/bitwise.c

libbitwise_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libbitwise_plugin_la_LIBADD = libslapd.la
libbitwise_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libchainingdb-plugin
#------------------------
libchainingdb_plugin_la_SOURCES = ldap/servers/plugins/chainingdb/cb_abandon.c \
	ldap/servers/plugins/chainingdb/cb_acl.c \
	ldap/servers/plugins/chainingdb/cb_add.c \
	ldap/servers/plugins/chainingdb/cb_bind.c \
	ldap/servers/plugins/chainingdb/cb_cleanup.c \
	ldap/servers/plugins/chainingdb/cb_close.c \
	ldap/servers/plugins/chainingdb/cb_compare.c \
	ldap/servers/plugins/chainingdb/cb_config.c \
	ldap/servers/plugins/chainingdb/cb_conn_stateless.c \
	ldap/servers/plugins/chainingdb/cb_controls.c \
	ldap/servers/plugins/chainingdb/cb_debug.c \
	ldap/servers/plugins/chainingdb/cb_delete.c \
	ldap/servers/plugins/chainingdb/cb_init.c \
	ldap/servers/plugins/chainingdb/cb_instance.c \
	ldap/servers/plugins/chainingdb/cb_modify.c \
	ldap/servers/plugins/chainingdb/cb_modrdn.c \
	ldap/servers/plugins/chainingdb/cb_monitor.c \
	ldap/servers/plugins/chainingdb/cb_schema.c \
	ldap/servers/plugins/chainingdb/cb_search.c \
	ldap/servers/plugins/chainingdb/cb_size.c \
	ldap/servers/plugins/chainingdb/cb_start.c \
	ldap/servers/plugins/chainingdb/cb_temp.c \
	ldap/servers/plugins/chainingdb/cb_test.c \
	ldap/servers/plugins/chainingdb/cb_unbind.c \
	ldap/servers/plugins/chainingdb/cb_utils.c 

libchainingdb_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libchainingdb_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libchainingdb_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libcollation-plugin
#------------------------
libcollation_plugin_la_SOURCES = ldap/servers/plugins/collation/collate.c \
        ldap/servers/plugins/collation/config.c \
        ldap/servers/plugins/collation/orfilter.c

libcollation_plugin_la_CPPFLAGS = @icu_inc@ $(PLUGIN_CPPFLAGS)
libcollation_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(ICU_LINK) $(LIBCSTD) $(LIBCRUN)
libcollation_plugin_la_LDFLAGS = -avoid-version
libcollation_plugin_la_LINK = $(CXXLINK) -avoid-version

#------------------------
# libcos-plugin
#------------------------
libcos_plugin_la_SOURCES = ldap/servers/plugins/cos/cos.c \
	ldap/servers/plugins/cos/cos_cache.c

libcos_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libcos_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libcos_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libderef-plugin
#-----------------------
libderef_plugin_la_SOURCES = ldap/servers/plugins/deref/deref.c

libderef_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libderef_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libderef_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdes-plugin
#-----------------------
libdes_plugin_la_SOURCES = ldap/servers/plugins/rever/des.c \
	ldap/servers/plugins/rever/rever.c

libdes_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @svrcore_inc@
libdes_plugin_la_LIBADD = libslapd.la $(NSS_LINK)
libdes_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdistrib-plugin
#------------------------
libdistrib_plugin_la_SOURCES = ldap/servers/plugins/distrib/distrib.c

libdistrib_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libdistrib_plugin_la_LIBADD = libslapd.la
libdistrib_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdna-plugin
#------------------------
libdna_plugin_la_SOURCES = ldap/servers/plugins/dna/dna.c

libdna_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libdna_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libdna_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libhttp-client-plugin
#------------------------
libhttp_client_plugin_la_SOURCES = ldap/servers/plugins/http/http_client.c \
	ldap/servers/plugins/http/http_impl.c

libhttp_client_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libhttp_client_plugin_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK)
libhttp_client_plugin_la_LDFLAGS = -avoid-version

#------------------------
# liblinkedattrs-plugin
#------------------------
liblinkedattrs_plugin_la_SOURCES = ldap/servers/plugins/linkedattrs/fixup_task.c \
	ldap/servers/plugins/linkedattrs/linked_attrs.c

liblinkedattrs_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
liblinkedattrs_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
liblinkedattrs_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libmanagedentries-plugin
#------------------------
libmanagedentries_plugin_la_SOURCES = ldap/servers/plugins/mep/mep.c

libmanagedentries_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libmanagedentries_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libmanagedentries_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libmemberof-plugin
#------------------------
libmemberof_plugin_la_SOURCES= ldap/servers/plugins/memberof/memberof.c \
	ldap/servers/plugins/memberof/memberof_config.c

libmemberof_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libmemberof_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libmemberof_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpam-passthru-plugin
#------------------------
libpam_passthru_plugin_la_SOURCES = ldap/servers/plugins/pam_passthru/pam_ptconfig.c \
	ldap/servers/plugins/pam_passthru/pam_ptdebug.c \
	ldap/servers/plugins/pam_passthru/pam_ptimpl.c \
	ldap/servers/plugins/pam_passthru/pam_ptpreop.c

libpam_passthru_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libpam_passthru_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(PAM_LINK)
libpam_passthru_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpassthru-plugin
#------------------------
libpassthru_plugin_la_SOURCES = ldap/servers/plugins/passthru/ptbind.c \
	ldap/servers/plugins/passthru/ptconfig.c \
	ldap/servers/plugins/passthru/ptconn.c \
	ldap/servers/plugins/passthru/ptdebug.c \
	ldap/servers/plugins/passthru/ptpreop.c \
	ldap/servers/plugins/passthru/ptutil.c

libpassthru_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libpassthru_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libpassthru_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpresence-plugin
#------------------------
libpresence_plugin_la_SOURCES = ldap/servers/plugins/presence/presence.c

libpresence_plugin_la_CPPFLAGS = -I$(srcdir)/ldap/servers/plugins/http $(PLUGIN_CPPFLAGS)
libpresence_plugin_la_LIBADD = libslapd.la
libpresence_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpwdstorage-plugin
#------------------------
libpwdstorage_plugin_la_SOURCES = ldap/servers/plugins/pwdstorage/clear_pwd.c \
	ldap/servers/plugins/pwdstorage/crypt_pwd.c \
	ldap/servers/plugins/pwdstorage/md5_pwd.c \
	ldap/servers/plugins/pwdstorage/md5c.c \
	ldap/servers/plugins/pwdstorage/ns-mta-md5_pwd.c \
	ldap/servers/plugins/pwdstorage/pwd_init.c \
	ldap/servers/plugins/pwdstorage/pwd_util.c \
	ldap/servers/plugins/pwdstorage/sha_pwd.c \
	ldap/servers/plugins/pwdstorage/smd5_pwd.c \
	ldap/servers/plugins/pwdstorage/ssha_pwd.c

libpwdstorage_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libpwdstorage_plugin_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK) $(LIBCRYPT)
libpwdstorage_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libreferint-plugin
#------------------------
libreferint_plugin_la_SOURCES = ldap/servers/plugins/referint/referint.c

libreferint_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libreferint_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libreferint_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libreplication-plugin
#------------------------
libreplication_plugin_la_SOURCES = ldap/servers/plugins/replication/cl5_api.c \
	ldap/servers/plugins/replication/cl5_clcache.c \
	ldap/servers/plugins/replication/cl5_config.c \
	ldap/servers/plugins/replication/cl5_init.c \
	ldap/servers/plugins/replication/cl_crypt.c \
	ldap/servers/plugins/replication/csnpl.c \
	ldap/servers/plugins/replication/legacy_consumer.c \
	ldap/servers/plugins/replication/llist.c \
	ldap/servers/plugins/replication/repl_add.c \
	ldap/servers/plugins/replication/repl_bind.c \
	ldap/servers/plugins/replication/repl_compare.c \
	ldap/servers/plugins/replication/repl_connext.c \
	ldap/servers/plugins/replication/repl_controls.c \
	ldap/servers/plugins/replication/repl_delete.c \
	ldap/servers/plugins/replication/repl_entry.c \
	ldap/servers/plugins/replication/repl_ext.c \
	ldap/servers/plugins/replication/repl_extop.c \
	ldap/servers/plugins/replication/repl_globals.c \
	ldap/servers/plugins/replication/repl_init.c \
	ldap/servers/plugins/replication/repl_modify.c \
	ldap/servers/plugins/replication/repl_modrdn.c \
	ldap/servers/plugins/replication/repl_monitor.c \
	ldap/servers/plugins/replication/repl_objset.c \
	ldap/servers/plugins/replication/repl_opext.c \
	ldap/servers/plugins/replication/repl_ops.c \
	ldap/servers/plugins/replication/repl_rootdse.c \
	ldap/servers/plugins/replication/repl_search.c \
	ldap/servers/plugins/replication/repl_session_plugin.c \
	ldap/servers/plugins/replication/repl5_agmt.c \
	ldap/servers/plugins/replication/repl5_agmtlist.c \
	ldap/servers/plugins/replication/repl5_backoff.c \
	ldap/servers/plugins/replication/repl5_connection.c \
	ldap/servers/plugins/replication/repl5_inc_protocol.c \
	ldap/servers/plugins/replication/repl5_init.c \
	ldap/servers/plugins/replication/repl5_mtnode_ext.c \
	ldap/servers/plugins/replication/repl5_plugins.c \
	ldap/servers/plugins/replication/repl5_protocol.c \
	ldap/servers/plugins/replication/repl5_protocol_util.c \
	ldap/servers/plugins/replication/repl5_replica.c \
	ldap/servers/plugins/replication/repl5_replica_config.c \
	ldap/servers/plugins/replication/repl5_replica_dnhash.c \
	ldap/servers/plugins/replication/repl5_replica_hash.c \
	ldap/servers/plugins/replication/repl5_ruv.c \
	ldap/servers/plugins/replication/repl5_schedule.c \
	ldap/servers/plugins/replication/repl5_tot_protocol.c \
	ldap/servers/plugins/replication/repl5_total.c \
	ldap/servers/plugins/replication/repl5_updatedn_list.c \
	ldap/servers/plugins/replication/replutil.c \
	ldap/servers/plugins/replication/urp.c \
	ldap/servers/plugins/replication/urp_glue.c \
	ldap/servers/plugins/replication/urp_tombstone.c \
	ldap/servers/plugins/replication/windows_connection.c \
	ldap/servers/plugins/replication/windows_inc_protocol.c \
	ldap/servers/plugins/replication/windows_private.c \
	ldap/servers/plugins/replication/windows_protocol_util.c \
	ldap/servers/plugins/replication/windows_tot_protocol.c

libreplication_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @icu_inc@ @db_inc@
libreplication_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSS_LINK) $(NSPR_LINK) $(ICU_LINK) $(DB_LINK)
libreplication_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libretrocl-plugin
#------------------------
libretrocl_plugin_la_SOURCES = ldap/servers/plugins/retrocl/retrocl.c \
	ldap/servers/plugins/retrocl/retrocl_cn.c \
	ldap/servers/plugins/retrocl/retrocl_create.c \
	ldap/servers/plugins/retrocl/retrocl_po.c \
	ldap/servers/plugins/retrocl/retrocl_rootdse.c \
	ldap/servers/plugins/retrocl/retrocl_trim.c

libretrocl_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libretrocl_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libretrocl_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libroles-plugin
#------------------------
libroles_plugin_la_SOURCES = ldap/servers/plugins/roles/roles_cache.c \
	ldap/servers/plugins/roles/roles_plugin.c

libroles_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libroles_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libroles_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libschemareload-plugin
#------------------------
libschemareload_plugin_la_SOURCES = ldap/servers/plugins/schema_reload/schema_reload.c

libschemareload_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libschemareload_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libschemareload_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libstatechange-plugin
#------------------------
libstatechange_plugin_la_SOURCES = ldap/servers/plugins/statechange/statechange.c

libstatechange_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libstatechange_plugin_la_LIBADD = libslapd.la
libstatechange_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libsyntax-plugin
#------------------------
libsyntax_plugin_la_SOURCES = ldap/servers/plugins/syntaxes/bin.c \
	ldap/servers/plugins/syntaxes/bitstring.c \
	ldap/servers/plugins/syntaxes/ces.c \
	ldap/servers/plugins/syntaxes/cis.c \
	ldap/servers/plugins/syntaxes/debug.c \
	ldap/servers/plugins/syntaxes/dn.c \
	ldap/servers/plugins/syntaxes/deliverymethod.c \
	ldap/servers/plugins/syntaxes/facsimile.c \
	ldap/servers/plugins/syntaxes/guide.c \
	ldap/servers/plugins/syntaxes/int.c \
	ldap/servers/plugins/syntaxes/nameoptuid.c \
	ldap/servers/plugins/syntaxes/numericstring.c \
	ldap/servers/plugins/syntaxes/phonetic.c \
	ldap/servers/plugins/syntaxes/sicis.c \
	ldap/servers/plugins/syntaxes/string.c \
	ldap/servers/plugins/syntaxes/syntax_common.c \
	ldap/servers/plugins/syntaxes/tel.c \
	ldap/servers/plugins/syntaxes/telex.c \
	ldap/servers/plugins/syntaxes/teletex.c \
	ldap/servers/plugins/syntaxes/validate.c \
	ldap/servers/plugins/syntaxes/validate_task.c \
	ldap/servers/plugins/syntaxes/value.c

libsyntax_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libsyntax_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libsyntax_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libusn-plugin
#------------------------
libusn_plugin_la_SOURCES = ldap/servers/plugins/usn/usn.c \
                           ldap/servers/plugins/usn/usn_cleanup.c

libusn_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libusn_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libusn_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libviews-plugin
#------------------------
libviews_plugin_la_SOURCES = ldap/servers/plugins/views/views.c

libviews_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libviews_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libviews_plugin_la_LDFLAGS = -avoid-version


#////////////////////////////////////////////////////////////////
#
#   Programs
#
#////////////////////////////////////////////////////////////////
#------------------------
# dbscan
#------------------------
dbscan_bin_SOURCES = ldap/servers/slapd/tools/dbscan.c

dbscan_bin_CPPFLAGS = @db_inc@ @nspr_inc@ $(AM_CPPFLAGS)
dbscan_bin_LDADD = $(NSPR_LINK) $(DB_LINK)

#------------------------
# dsktune
#------------------------
dsktune_bin_SOURCES = ldap/systools/idsktune.c \
	ldap/systools/pio.c

#------------------------
# infadd
#------------------------
infadd_bin_SOURCES = ldap/servers/slapd/tools/rsearch/addthread.c \
	ldap/servers/slapd/tools/rsearch/infadd.c \
	ldap/servers/slapd/tools/rsearch/nametable.c

infadd_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
infadd_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBSOCKET)

#------------------------
# ldap-agent
#------------------------
ldap_agent_bin_SOURCES = ldap/servers/snmp/main.c \
	ldap/servers/snmp/ldap-agent.c \
	ldap/servers/slapd/agtmmap.c

ldap_agent_bin_CPPFLAGS = $(AM_CPPFLAGS) @netsnmp_inc@ @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ldap_agent_bin_LDADD = $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK) $(NETSNMP_LINK) $(THREADLIB)
if SOLARIS
ldap_agent_bin_LDADD += -lrt
endif


#------------------------
# ldclt
#------------------------
ldclt_bin_SOURCES = ldap/servers/slapd/tools/ldaptool-sasl.c \
	ldap/servers/slapd/tools/ldclt/data.c \
	ldap/servers/slapd/tools/ldclt/ldapfct.c \
	ldap/servers/slapd/tools/ldclt/ldclt.c \
	ldap/servers/slapd/tools/ldclt/ldcltU.c \
	ldap/servers/slapd/tools/ldclt/parser.c \
	ldap/servers/slapd/tools/ldclt/port.c \
	ldap/servers/slapd/tools/ldclt/scalab01.c \
	ldap/servers/slapd/tools/ldclt/threadMain.c \
	ldap/servers/slapd/tools/ldclt/utils.c \
	ldap/servers/slapd/tools/ldclt/version.c \
	ldap/servers/slapd/tools/ldclt/workarounds.c

if SOLARIS
ldclt_bin_SOURCES += ldap/servers/slapd/tools/ldclt/opCheck.c
endif

ldclt_bin_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ldap/servers/slapd/tools @openldap_inc@ @ldapsdk_inc@ @sasl_inc@ @nss_inc@ @nspr_inc@
ldclt_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBNSL) $(LIBSOCKET) $(LIBDL) $(THREADLIB)

#------------------------
# ldif
#------------------------
ldif_bin_SOURCES = ldap/servers/slapd/tools/ldif.c

ldif_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ldif_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# migratecred
#------------------------
migratecred_bin_SOURCES = ldap/servers/slapd/tools/migratecred.c

migratecred_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
migratecred_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# mmldif
#------------------------
mmldif_bin_SOURCES = ldap/servers/slapd/tools/mmldif.c

mmldif_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
mmldif_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# ns-slapd
#------------------------
if enable_ldapi
        GETSOCKETPEER=ldap/servers/slapd/getsocketpeer.c
        enable_ldapi = 1
endif
if enable_autobind
        enable_autobind = 1
endif
if enable_auto_dn_suffix
        enable_auto_dn_suffix = 1
endif

ns_slapd_SOURCES = ldap/servers/slapd/abandon.c \
	ldap/servers/slapd/auth.c \
	ldap/servers/slapd/bind.c  \
	ldap/servers/slapd/compare.c \
	ldap/servers/slapd/config.c \
	ldap/servers/slapd/configdse.c \
	ldap/servers/slapd/connection.c \
	ldap/servers/slapd/conntable.c \
	ldap/servers/slapd/daemon.c \
	ldap/servers/slapd/detach.c \
	ldap/servers/slapd/extendop.c \
	ldap/servers/slapd/fedse.c \
	ldap/servers/slapd/fileio.c \
	ldap/servers/slapd/getopt_ext.c \
	ldap/servers/slapd/globals.c \
	ldap/servers/slapd/house.c \
	ldap/servers/slapd/init.c \
	ldap/servers/slapd/main.c \
	ldap/servers/slapd/monitor.c \
	ldap/servers/slapd/passwd_extop.c \
	ldap/servers/slapd/protect_db.c \
	ldap/servers/slapd/psearch.c \
	ldap/servers/slapd/pw_mgmt.c \
	ldap/servers/slapd/rootdse.c \
	ldap/servers/slapd/sasl_io.c \
	ldap/servers/slapd/saslbind.c \
	ldap/servers/slapd/search.c \
	ldap/servers/slapd/start_tls_extop.c \
	ldap/servers/slapd/strdup.c \
	ldap/servers/slapd/stubs.c \
	ldap/servers/slapd/tempnam.c  \
	ldap/servers/slapd/unbind.c \
	$(GETSOCKETPEER)

ns_slapd_CPPFLAGS = $(AM_CPPFLAGS) @sasl_inc@ @openldap_inc@ @ldapsdk_inc@ @nss_inc@ \
	@nspr_inc@ @svrcore_inc@
ns_slapd_LDADD = libslapd.la libldaputil.a $(LDAPSDK_LINK) $(NSS_LINK) \
	$(NSPR_LINK) $(SASL_LINK) $(SVRCORE_LINK) $(LIBNSL) $(LIBSOCKET) $(THREADLIB)
# We need to link ns-slapd with the C++ compiler on HP-UX since we load
# some C++ shared libraries (such as icu).
if HPUX
ns_slapd_LINK = $(CXXLINK)
else
ns_slapd_LINK = $(LINK)
endif

#------------------------
# pwdhash
#------------------------
pwdhash_bin_SOURCES = ldap/servers/slapd/tools/pwenc.c

pwdhash_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
pwdhash_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# rsearch
#------------------------
rsearch_bin_SOURCES = ldap/servers/slapd/tools/rsearch/nametable.c \
	ldap/servers/slapd/tools/rsearch/rsearch.c \
	ldap/servers/slapd/tools/rsearch/sdattable.c \
	ldap/servers/slapd/tools/rsearch/searchthread.c

rsearch_bin_CPPFLAGS = $(AM_CPPFLAGS) @openldap_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
rsearch_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBSOCKET)

# these are for the config files and scripts that we need to generate and replace
# the paths and other tokens with the real values set during configure/make
# note that we cannot just use AC_OUTPUT to do this for us, since it will do things like this:
# LD_LIBRARY_PATH = ${prefix}/lib/dirsrv
# i.e. it literally copies in '${prefix}' rather than expanding it out - we want this instead:
# LD_LIBRARY_PATH = /usr/lib/dirsrv
if BUNDLE
# on the systems on which we bundle, perldap will be in libdir/perl, so we need to point
# the perlpath there - on other systems, perldap will be installed as a site module into
# the system perl
fixupcmd = sed \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@nspr_libdir\@,$(libdir),g' \
	-e 's,@nss_libdir\@,$(libdir),g' \
	-e 's,@ldapsdk_libdir\@,$(libdir),g' \
	-e 's,@ldapsdk_bindir\@,$(bindir),g' \
	-e 's,@ldaptool_bindir\@,$(bindir),g' \
	-e 's,@ldaptool_opts\@,$(ldaptool_opts),g' \
	-e 's,@plainldif_opts\@,$(plainldif_opts),g' \
	-e 's,@db_libdir\@,$(libdir),g' \
	-e 's,@db_bindir\@,$(bindir),g' \
	-e 's,@sasl_libdir\@,$(libdir),g' \
	-e 's,@sasl_path\@,$(libdir)/sasl2,g' \
	-e 's,@netsnmp_libdir\@,$(libdir),g' \
	-e 's,@pcre_libdir\@,$(libdir),g' \
	-e 's,@propertydir\@,$(propertydir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@schemadir\@,$(schemadir),g' \
	-e 's,@serverdir\@,$(serverdir),g' \
	-e 's,@serverplugindir\@,$(serverplugindir),g' \
	-e 's,@taskdir\@,$(taskdir),g' \
	-e 's,@configdir\@,$(configdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@infdir\@,$(infdir),g' \
	-e 's,@mibdir\@,$(mibdir),g' \
	-e 's,@templatedir\@,$(sampledatadir),g' \
	-e 's,@package_name\@,$(PACKAGE_NAME),g' \
	-e 's,@instconfigdir\@,$(instconfigdir),g' \
	-e 's,@enable_ldapi\@,$(enable_ldapi),g' \
	-e 's,@enable_pam_passthru\@,$(enable_pam_passthru),g' \
	-e 's,@enable_bitwise\@,$(enable_bitwise),g' \
	-e 's,@enable_dna\@,$(enable_dna),g' \
	-e 's,@enable_autobind\@,$(enable_autobind),g' \
	-e 's,@enable_auto_dn_suffix\@,$(enable_auto_dn_suffix),g' \
	-e 's,@enable_presence\@,$(enable_presence),g' \
	-e 's,@ECHO_N\@,$(ECHO_N),g' \
	-e 's,@ECHO_C\@,$(ECHO_C),g' \
	-e 's,@brand\@,$(brand),g' \
	-e 's,@capbrand\@,$(capbrand),g' \
	-e 's,@vendor\@,$(vendor),g' \
	-e 's,@PACKAGE_NAME\@,$(PACKAGE_NAME),g' \
	-e 's,@PACKAGE_VERSION\@,$(PACKAGE_VERSION),g' \
	-e 's,@PACKAGE_BASE_VERSION\@,$(PACKAGE_BASE_VERSION),g' \
	-e 's,@CONSOLE_VERSION\@,$(CONSOLE_VERSION),g' \
	-e 's,@BUILDNUM\@,$(BUILDNUM),g' \
	-e 's,@NQBUILD_NUM\@,$(NQBUILDNUM),g' \
	-e 's,@perlpath\@,$(perldir) $(libdir)/perl/arch $(libdir)/perl,g' \
	-e 's,@defaultuser\@,$(defaultuser),g' \
	-e 's,@defaultgroup\@,$(defaultgroup),g' \
	-e 's,@with_fhs_opt\@,@with_fhs_opt@,g' \
	-e 's,@with_selinux\@,@with_selinux@,g' \
	-e 's,@perlexec\@,@perlexec@,g' \
	-e 's,@initconfigdir\@,$(initconfigdir),g'\
	-e 's,@updatedir\@,$(updatedir),g'
else
fixupcmd = sed \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@nspr_libdir\@,$(nspr_libdir),g' \
	-e 's,@nss_libdir\@,$(nss_libdir),g' \
	-e 's,@ldapsdk_libdir\@,$(ldapsdk_libdir),g' \
	-e 's,@ldapsdk_bindir\@,$(ldapsdk_bindir),g' \
	-e 's,@ldaptool_bindir\@,$(ldaptool_bindir),g' \
	-e 's,@ldaptool_opts\@,$(ldaptool_opts),g' \
	-e 's,@plainldif_opts\@,$(plainldif_opts),g' \
	-e 's,@db_libdir\@,$(db_libdir),g' \
	-e 's,@db_bindir\@,$(db_bindir),g' \
	-e 's,@sasl_libdir\@,$(sasl_libdir),g' \
	-e 's,@sasl_path\@,@sasl_path@,g' \
	-e 's,@netsnmp_libdir\@,$(netsnmp_libdir),g' \
	-e 's,@pcre_libdir\@,$(pcre_libdir),g' \
	-e 's,@propertydir\@,$(propertydir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@schemadir\@,$(schemadir),g' \
	-e 's,@serverdir\@,$(serverdir),g' \
	-e 's,@serverplugindir\@,$(serverplugindir),g' \
	-e 's,@taskdir\@,$(taskdir),g' \
	-e 's,@configdir\@,$(configdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@infdir\@,$(infdir),g' \
	-e 's,@mibdir\@,$(mibdir),g' \
	-e 's,@templatedir\@,$(sampledatadir),g' \
	-e 's,@package_name\@,$(PACKAGE_NAME),g' \
	-e 's,@instconfigdir\@,$(instconfigdir),g' \
	-e 's,@enable_ldapi\@,$(enable_ldapi),g' \
	-e 's,@enable_pam_passthru\@,$(enable_pam_passthru),g' \
	-e 's,@enable_bitwise\@,$(enable_bitwise),g' \
	-e 's,@enable_dna\@,$(enable_dna),g' \
	-e 's,@enable_autobind\@,$(enable_autobind),g' \
	-e 's,@enable_auto_dn_suffix\@,$(enable_auto_dn_suffix),g' \
	-e 's,@enable_presence\@,$(enable_presence),g' \
	-e 's,@ECHO_N\@,$(ECHO_N),g' \
	-e 's,@ECHO_C\@,$(ECHO_C),g' \
	-e 's,@brand\@,$(brand),g' \
	-e 's,@capbrand\@,$(capbrand),g' \
	-e 's,@vendor\@,$(vendor),g' \
	-e 's,@PACKAGE_NAME\@,$(PACKAGE_NAME),g' \
	-e 's,@PACKAGE_VERSION\@,$(PACKAGE_VERSION),g' \
	-e 's,@PACKAGE_BASE_VERSION\@,$(PACKAGE_BASE_VERSION),g' \
	-e 's,@CONSOLE_VERSION\@,$(CONSOLE_VERSION),g' \
	-e 's,@BUILDNUM\@,$(BUILDNUM),g' \
	-e 's,@NQBUILD_NUM\@,$(NQBUILDNUM),g' \
	-e 's,@perlpath\@,$(perldir),g' \
	-e 's,@defaultuser\@,$(defaultuser),g' \
	-e 's,@defaultgroup\@,$(defaultgroup),g' \
	-e 's,@with_fhs_opt\@,@with_fhs_opt@,g' \
	-e 's,@with_selinux\@,@with_selinux@,g' \
	-e 's,@perlexec\@,@perlexec@,g' \
	-e 's,@initconfigdir\@,$(initconfigdir),g' \
	-e 's,@updatedir\@,$(updatedir),g'
endif

%: %.in
	mkdir -p $(dir $@)
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME): %/initscript.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME): %/initconfig.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME)-snmp: %/ldap-agent-initscript.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

# if distdir is a git tag, use that for the git archive tag, else
# just assume a developer build and use HEAD
git-archive:
	if [ -n "$(SRCDISTDIR)" -a -d "$(SRCDISTDIR)" ] ; then \
		srcdistdir=$(SRCDISTDIR) ; \
	else \
		srcdistdir=`pwd` ; \
	fi ; \
	cd $(srcdir) ; \
	if git show-ref --tags -q $(distdir) ; then \
		gittag=$(distdir) ; \
	else \
		gittag=HEAD ; \
	fi ; \
	git archive --prefix=$(distdir)/ $$gittag | bzip2 > $$srcdistdir/$(distdir).tar.bz2
