# look for included m4 files in the ./m4/ directory
ACLOCAL_AMFLAGS = -I m4

#------------------------
# Compiler Flags
#------------------------
BUILDNUM := $(shell perl $(srcdir)/buildnum.pl)
DEBUG_DEFINES = @debug_defs@
DS_DEFINES = -DBUILD_NUM=$(BUILDNUM)
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
	-DSBINDIR="\"$(sbindir)\"" -DPLUGINDIR="\"$(serverplugindir)\""

AM_CPPFLAGS = $(DEBUG_DEFINES) $(DS_DEFINES) $(DS_INCLUDES) $(PATH_DEFINES)
PLUGIN_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
# We need to make sure that libpthread is linked before libc on HP-UX.
if HPUX
AM_LDFLAGS = -lpthread
endif

#------------------------
# Linker Flags
#------------------------
NSPR_LINK = @nspr_lib@ -lplc4 -lplds4 -lnspr4
NSS_LINK = @nss_lib@ -lssl3 -lnss3 -lsoftokn3
LDAPSDK_LINK = @ldapsdk_lib@ -lssldap60 -lprldap60 -lldap60 -lldif60
DB_LINK = @db_lib@ -ldb-@db_libver@
SASL_LINK = @sasl_lib@ -lsasl2
SVRCORE_LINK = @svrcore_lib@ -lsvrcore
ICU_LINK = @icu_lib@ -licui18n -licuuc -licudata
NETSNMP_LINK = @netsnmp_lib@ @netsnmp_link@
PAM_LINK = -lpam

LIBSOCKET=@LIBSOCKET@
LIBNSL=@LIBNSL@
LIBDL=@LIBDL@
LIBCSTD=@LIBCSTD@
LIBCRUN=@LIBCRUN@

#------------------------
# Generated Sources
#------------------------
BUILT_SOURCES = dirver.h dberrstrs.h

CLEANFILES =  dirver.h dberrstrs.h ns-slapd.properties

dirver.h: Makefile
	perl $(srcdir)/dirver.pl -v "$(VERSION)" -o dirver.h

dberrstrs.h: Makefile
	perl $(srcdir)/ldap/servers/slapd/mkDBErrStrs.pl -i @db_incdir@ -o .

#------------------------
# Install Paths
#------------------------
configdir = $(sysconfdir)@configdir@
sampledatadir = $(datadir)@sampledatadir@
propertydir = $(sysconfdir)@propertydir@
schemadir = $(sysconfdir)@schemadir@
serverdir = $(libdir)@serverdir@
serverplugindir = $(libdir)@serverplugindir@
taskdir = $(datadir)@scripttemplatedir@
initdir = $(sysconfdir)@initdir@
instconfigdir = @instconfigdir@

#------------------------
# Build Products
#------------------------
sbin_PROGRAMS = ns-slapd

bin_PROGRAMS = dbscan-bin ds_newinst-bin dsktune-bin infadd-bin ldap-agent-bin \
	ldclt-bin ldif-bin migratecred-bin mmldif-bin pwdhash-bin rsearch-bin

server_LTLIBRARIES = libslapd.la libds_admin.la libns-dshttpd.la

# this is how to add optional plugins
if enable_pam_passthru
LIBPAM_PASSTHRU_PLUGIN = libpam-passthru-plugin.la
endif
if enable_dna
LIBDNA_PLUGIN = libdna-plugin.la
endif

if enable_bitwise
LIBBITWISE_PLUGIN = libbitwise-plugin.la
endif

serverplugin_LTLIBRARIES = libacl-plugin.la libattr-unique-plugin.la \
	libback-ldbm.la libchainingdb-plugin.la libcos-plugin.la libdes-plugin.la \
	libdistrib-plugin.la libhttp-client-plugin.la libcollation-plugin.la \
	libpassthru-plugin.la libpresence-plugin.la \
	libpwdstorage-plugin.la libreferint-plugin.la libreplication-plugin.la \
	libretrocl-plugin.la libroles-plugin.la libstatechange-plugin.la libsyntax-plugin.la \
	libviews-plugin.la $(LIBPAM_PASSTHRU_PLUGIN) $(LIBDNA_PLUGIN) $(LIBBITWISE_PLUGIN)

nodist_property_DATA = ns-slapd.properties

noinst_PROGRAMS = makstrdb

noinst_LIBRARIES = libavl.a libldaputil.a

#------------------------
# Installed Files
#------------------------
config_DATA = $(srcdir)/lib/ldaputil/certmap.conf \
	$(srcdir)/ldap/schema/slapd-collations.conf

sampledata_DATA = $(srcdir)/ldap/ldif/Ace.ldif \
	$(srcdir)/ldap/ldif/commonTasks.ldif \
	$(srcdir)/ldap/ldif/European.ldif \
	$(srcdir)/ldap/ldif/Eurosuffix.ldif \
	$(srcdir)/ldap/ldif/Example.ldif \
	$(srcdir)/ldap/ldif/Example-roles.ldif \
	$(srcdir)/ldap/ldif/Example-views.ldif \
	$(srcdir)/ldap/ldif/roledit.ldif \
	$(srcdir)/ldap/ldif/tasks.ldif \
	$(srcdir)/ldap/ldif/template.ldif \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-FamilyNames \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-GivenNames \
	$(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen-OrgUnits

schema_DATA = $(srcdir)/ldap/schema/00core.ldif \
	$(srcdir)/ldap/schema/01common.ldif \
	$(srcdir)/ldap/schema/05rfc2247.ldif \
	$(srcdir)/ldap/schema/05rfc2927.ldif \
	$(srcdir)/ldap/schema/10presence.ldif \
	$(srcdir)/ldap/schema/10rfc2307.ldif \
	$(srcdir)/ldap/schema/20subscriber.ldif \
	$(srcdir)/ldap/schema/25java-object.ldif \
	$(srcdir)/ldap/schema/28pilot.ldif \
	$(srcdir)/ldap/schema/30ns-common.ldif \
	$(srcdir)/ldap/schema/50ns-admin.ldif \
	$(srcdir)/ldap/schema/50ns-certificate.ldif \
	$(srcdir)/ldap/schema/50ns-directory.ldif \
	$(srcdir)/ldap/schema/50ns-value.ldif \
	$(srcdir)/ldap/schema/50ns-web.ldif \
	$(srcdir)/ldap/schema/60pam-plugin.ldif \
	$(srcdir)/ldap/schema/99user.ldif

bin_SCRIPTS = $(srcdir)/ldap/servers/slapd/tools/rsearch/scripts/dbgen.pl \
	wrappers/dbscan \
	wrappers/ds_newinst \
	ldap/admin/src/ds_newinst.pl \
	wrappers/dsktune \
	wrappers/infadd \
	wrappers/ldap-agent \
	wrappers/ldclt \
	wrappers/ldif \
	$(srcdir)/ldap/admin/src/logconv.pl \
	ldap/admin/src/migrateTo11 \
	wrappers/migratecred \
	wrappers/mmldif \
	wrappers/pwdhash \
	wrappers/rsearch \
	wrappers/cl-dump \
	ldap/admin/src/scripts/cl-dump.pl \
	wrappers/repl-monitor \
	ldap/admin/src/scripts/repl-monitor.pl

task_SCRIPTS = ldap/admin/src/scripts/template-bak2db \
	ldap/admin/src/scripts/template-db2bak \
	ldap/admin/src/scripts/template-db2index \
	ldap/admin/src/scripts/template-db2ldif \
	ldap/admin/src/scripts/template-ldif2db \
	ldap/admin/src/scripts/template-ldif2ldap \
	ldap/admin/src/scripts/template-monitor \
	ldap/admin/src/scripts/template-restoreconfig \
	ldap/admin/src/scripts/template-saveconfig \
	ldap/admin/src/scripts/template-start-slapd \
	ldap/admin/src/scripts/template-stop-slapd \
	ldap/admin/src/scripts/template-suffix2instance \
	ldap/admin/src/scripts/template-vlvindex \
	ldap/admin/src/scripts/template-bak2db.pl \
	ldap/admin/src/scripts/template-db2bak.pl \
	ldap/admin/src/scripts/template-db2index.pl \
	ldap/admin/src/scripts/template-db2ldif.pl \
	ldap/admin/src/scripts/template-ldif2db.pl \
	ldap/admin/src/scripts/template-ns-accountstatus.pl \
	ldap/admin/src/scripts/template-ns-activate.pl \
	ldap/admin/src/scripts/template-ns-inactivate.pl \
	ldap/admin/src/scripts/template-ns-newpwpolicy.pl \
	ldap/admin/src/scripts/template-verify-db.pl \
	$(srcdir)/ldap/admin/src/scripts/template-migrate5to7 \
	$(srcdir)/ldap/admin/src/scripts/template-migrate6to7 \
	$(srcdir)/ldap/admin/src/scripts/template-migrateInstance7 \
	$(srcdir)/ldap/admin/src/scripts/template-migrateTo7

init_SCRIPTS = wrappers/$(PACKAGE_NAME)

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
	lib/ldaputil/ldapdb.c \
	lib/ldaputil/vtable.c

libldaputil_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/lib/ldaputil @ldapsdk_inc@ @nss_inc@ @nspr_inc@


#////////////////////////////////////////////////////////////////
#
#   Dynamic Server Libraries
#
#////////////////////////////////////////////////////////////////

#------------------------
# libds_admin
#------------------------
libds_admin_la_SOURCES = ldap/admin/lib/dsalib_conf.c \
	ldap/admin/lib/dsalib_confs.c \
	ldap/admin/lib/dsalib_db.c \
	ldap/admin/lib/dsalib_debug.c \
	ldap/admin/lib/dsalib_dn.c \
	ldap/admin/lib/dsalib_filename.c \
	ldap/admin/lib/dsalib_html.c \
	ldap/admin/lib/dsalib_ldif.c \
	ldap/admin/lib/dsalib_location.c \
	ldap/admin/lib/dsalib_pw.c \
	ldap/admin/lib/dsalib_tailf.c \
	ldap/admin/lib/dsalib_updown.c \
	ldap/admin/lib/dsalib_util.c

libds_admin_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ldap/admin/include @ldapsdk_inc@ @nss_inc@ @nspr_inc@
libds_admin_la_LIBADD = $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK)

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
	lib/libaccess/ldapacl.cpp \
	lib/libaccess/method.cpp \
	lib/libaccess/nseframe.cpp \
	lib/libaccess/nsautherr.cpp \
	lib/libaccess/oneeval.cpp \
	lib/libaccess/register.cpp \
	lib/libaccess/symbols.cpp \
	lib/libaccess/usi.cpp \
	lib/libaccess/usrcache.cpp \
	lib/libadmin/authdb.c \
	lib/libadmin/error.c \
	lib/libadmin/template.c \
	lib/libadmin/util.c \
	lib/base/crit.cpp \
	lib/base/dns.cpp \
	lib/base/dnsdmain.cpp \
	lib/base/ereport.cpp \
	lib/base/file.cpp \
	lib/base/fsmutex.cpp \
	lib/base/lexer.cpp \
	lib/base/net.cpp \
	lib/base/nscperror.c \
	lib/base/plist.cpp \
	lib/base/pool.cpp \
	lib/base/rwlock.cpp \
	lib/base/shexp.cpp \
	lib/base/system.cpp \
	lib/base/systhr.cpp \
	lib/base/util.cpp \
	lib/libsi18n/coreres.c \
	lib/libsi18n/getstrprop.c \
	lib/libsi18n/getlang.c \
	lib/libsi18n/propset.c \
	lib/libsi18n/reshash.c \
	lib/libsi18n/txtfile.c \
	$(libldaputil_a_SOURCES)

libns_dshttpd_la_CPPFLAGS = -I$(srcdir)/include/base $(AM_CPPFLAGS) -I$(srcdir)/lib/ldaputil @ldapsdk_inc@ @nss_inc@ @nspr_inc@
libns_dshttpd_la_LIBADD = $(LDAPSDK_LINK) $(NSS_LINK) $(NSPR_LINK)

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
	ldap/servers/slapd/pblock.c \
	ldap/servers/slapd/plugin.c \
	ldap/servers/slapd/plugin_acl.c \
	ldap/servers/slapd/plugin_internal_op.c \
	ldap/servers/slapd/plugin_mr.c \
	ldap/servers/slapd/plugin_role.c \
	ldap/servers/slapd/plugin_syntax.c \
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
	ldap/servers/slapd/slapi2nspr.c \
	ldap/servers/slapd/snmp_collator.c \
	ldap/servers/slapd/ssl.c \
	ldap/servers/slapd/str2filter.c \
	ldap/servers/slapd/subentry.c \
	ldap/servers/slapd/task.c \
	ldap/servers/slapd/time.c \
	ldap/servers/slapd/uniqueid.c \
	ldap/servers/slapd/uniqueidgen.c \
	ldap/servers/slapd/utf8compare.c \
	ldap/servers/slapd/util.c \
	ldap/servers/slapd/uuid.c \
	ldap/servers/slapd/value.c \
	ldap/servers/slapd/valueset.c \
	ldap/servers/slapd/vattr.c \
	$(libavl_a_SOURCES)

libslapd_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @db_inc@ @svrcore_inc@
libslapd_la_LIBADD = $(LDAPSDK_LINK) $(SASL_LINK) $(SVRCORE_LINK) $(NSS_LINK) $(NSPR_LINK)


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
	ldap/servers/slapd/back-ldbm/dbtest.c \
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
	ldap/servers/slapd/back-ldbm/ldbm_index_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_instance_config.c \
	ldap/servers/slapd/back-ldbm/ldbm_modify.c \
	ldap/servers/slapd/back-ldbm/ldbm_modrdn.c \
	ldap/servers/slapd/back-ldbm/ldbm_search.c \
	ldap/servers/slapd/back-ldbm/ldbm_unbind.c \
	ldap/servers/slapd/back-ldbm/ldif2ldbm.c \
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
libback_ldbm_la_LIBADD = libslapd.la $(DB_LINK)
libback_ldbm_la_LDFLAGS = -avoid-version

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
	ldap/servers/plugins/acl/aclproxy.c \
	ldap/servers/plugins/acl/aclutil.c

libacl_plugin_la_CPPFLAGS = -I$(srcdir)/include/libaccess $(PLUGIN_CPPFLAGS)
libacl_plugin_la_LIBADD = libns-dshttpd.la $(LIBCSTD) $(LIBCRUN)
libacl_plugin_la_LDFLAGS = -avoid-version
libacl_plugin_la_LINK = $(CXXLINK)

#------------------------
# libattr-unique-plugin
#------------------------
libattr_unique_plugin_la_SOURCES = ldap/servers/plugins/uiduniq/7bit.c \
	ldap/servers/plugins/uiduniq/uid.c \
	ldap/servers/plugins/shared/utils.c

libattr_unique_plugin_la_CPPFLAGS = -I$(srcdir)/ldap/servers/plugins/shared $(PLUGIN_CPPFLAGS)
libattr_unique_plugin_la_LDFLAGS = -avoid-version

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
libchainingdb_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libcos-plugin
#------------------------
libcos_plugin_la_SOURCES = ldap/servers/plugins/cos/cos.c \
	ldap/servers/plugins/cos/cos_cache.c

libcos_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libcos_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdes-plugin
#-----------------------
libdes_plugin_la_SOURCES = ldap/servers/plugins/rever/des.c \
	ldap/servers/plugins/rever/rever.c

libdes_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS) @svrcore_inc@
libdes_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdistrib-plugin
#------------------------
libdistrib_plugin_la_SOURCES = ldap/servers/plugins/distrib/distrib.c

libdistrib_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libdistrib_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libhttp-client-plugin
#------------------------
libhttp_client_plugin_la_SOURCES = ldap/servers/plugins/http/http_client.c \
	ldap/servers/plugins/http/http_impl.c

libhttp_client_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libhttp_client_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libcollation-plugin
#------------------------
libcollation_plugin_la_SOURCES = ldap/servers/plugins/collation/collate.c \
	ldap/servers/plugins/collation/config.c \
	ldap/servers/plugins/collation/orfilter.c

libcollation_plugin_la_CPPFLAGS = @icu_inc@ $(PLUGIN_CPPFLAGS)
libcollation_plugin_la_LIBADD = $(ICU_LINK) $(LIBCSTD) $(LIBCRUN)
libcollation_plugin_la_LDFLAGS = -avoid-version
libcollation_plugin_la_LINK = $(CXXLINK)

#------------------------
# libpam-passthru-plugin
#------------------------
libpam_passthru_plugin_la_SOURCES = ldap/servers/plugins/pam_passthru/pam_ptconfig.c \
	ldap/servers/plugins/pam_passthru/pam_ptdebug.c \
	ldap/servers/plugins/pam_passthru/pam_ptimpl.c \
	ldap/servers/plugins/pam_passthru/pam_ptpreop.c

libpam_passthru_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libpam_passthru_plugin_la_LIBADD = $(PAM_LINK)
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
libpassthru_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpresence-plugin
#------------------------
libpresence_plugin_la_SOURCES = ldap/servers/plugins/presence/presence.c

libpresence_plugin_la_CPPFLAGS = -I$(srcdir)/ldap/servers/plugins/http $(PLUGIN_CPPFLAGS)
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
	ldap/servers/plugins/pwdstorage/sha_pwd.c \
	ldap/servers/plugins/pwdstorage/ssha_pwd.c

libpwdstorage_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libpwdstorage_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libreferint-plugin
#------------------------
libreferint_plugin_la_SOURCES = ldap/servers/plugins/referint/referint.c

libreferint_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libreferint_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libreplication-plugin
#------------------------
libreplication_plugin_la_SOURCES = ldap/servers/plugins/replication/cl5_api.c \
	ldap/servers/plugins/replication/cl5_clcache.c \
	ldap/servers/plugins/replication/cl5_config.c \
	ldap/servers/plugins/replication/cl5_init.c \
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
libreplication_plugin_la_LIBADD = $(ICU_LINK) $(DB_LINK)
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
libretrocl_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libroles-plugin
#------------------------
libroles_plugin_la_SOURCES = ldap/servers/plugins/roles/roles_cache.c \
	ldap/servers/plugins/roles/roles_plugin.c

libroles_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libroles_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libstatechange-plugin
#------------------------
libstatechange_plugin_la_SOURCES = ldap/servers/plugins/statechange/statechange.c

libstatechange_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libstatechange_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libsyntax-plugin
#------------------------
libsyntax_plugin_la_SOURCES = ldap/servers/plugins/syntaxes/bin.c \
	ldap/servers/plugins/syntaxes/ces.c \
	ldap/servers/plugins/syntaxes/cis.c \
	ldap/servers/plugins/syntaxes/debug.c \
	ldap/servers/plugins/syntaxes/dn.c \
	ldap/servers/plugins/syntaxes/int.c \
	ldap/servers/plugins/syntaxes/phonetic.c \
	ldap/servers/plugins/syntaxes/sicis.c \
	ldap/servers/plugins/syntaxes/string.c \
	ldap/servers/plugins/syntaxes/tel.c \
	ldap/servers/plugins/syntaxes/value.c

libsyntax_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libsyntax_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libviews-plugin
#------------------------
libviews_plugin_la_SOURCES = ldap/servers/plugins/views/views.c

libviews_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libviews_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdna-plugin
#------------------------
libdna_plugin_la_SOURCES = ldap/servers/plugins/dna/dna.c

libdna_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libdna_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libbitwise-plugin
#------------------------
libbitwise_plugin_la_SOURCES = ldap/servers/plugins/bitwise/bitwise.c

libbitwise_plugin_la_CPPFLAGS = $(PLUGIN_CPPFLAGS)
libbitwise_plugin_la_LDFLAGS = -avoid-version


#////////////////////////////////////////////////////////////////
#
#   Programs
#
#////////////////////////////////////////////////////////////////
#------------------------
# dbscan
#------------------------
dbscan_bin_SOURCES = ldap/servers/slapd/tools/dbscan.c

dbscan_bin_CPPFLAGS = @db_inc@ $(AM_CPPFLAGS)
dbscan_bin_LDADD = $(NSPR_LINK) $(DB_LINK)

#------------------------
# ds_newinst
#------------------------
ds_newinst_bin_SOURCES = ldap/admin/src/cfg_sspt.c \
	ldap/admin/src/create_instance.c \
	ldap/admin/src/ds_newinst.c \
	ldap/admin/src/script-gen.c

ds_newinst_bin_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ldap/admin/include @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ds_newinst_bin_LDADD = libds_admin.la $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBNSL) $(LIBSOCKET)

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

infadd_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
infadd_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBSOCKET)

#------------------------
# ldap-agent
#------------------------
ldap_agent_bin_SOURCES = ldap/servers/snmp/main.c \
	ldap/servers/snmp/ldap-agent.c \
	ldap/servers/slapd/agtmmap.c

ldap_agent_bin_CPPFLAGS = $(AM_CPPFLAGS) @netsnmp_inc@ @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ldap_agent_bin_LDADD = $(NETSNMP_LINK)

#------------------------
# ldclt
#------------------------
ldclt_bin_SOURCES = ldap/servers/slapd/tools/ldclt/data.c \
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

ldclt_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ldclt_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBNSL) $(LIBSOCKET) $(LIBDL)

#------------------------
# ldif
#------------------------
ldif_bin_SOURCES = ldap/servers/slapd/tools/ldif.c

ldif_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
ldif_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# migratecred
#------------------------
migratecred_bin_SOURCES = ldap/servers/slapd/tools/migratecred.c

migratecred_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
migratecred_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# mmldif
#------------------------
mmldif_bin_SOURCES = ldap/servers/slapd/tools/mmldif.c

mmldif_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
mmldif_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# ns-slapd
#------------------------
if enable_ldapi
        GETSOCKETPEER=ldap/servers/slapd/getsocketpeer.c
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
	ldap/servers/slapd/lite_entries.c \
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

ns_slapd_CPPFLAGS = $(AM_CPPFLAGS) @sasl_inc@ @ldapsdk_inc@ @nss_inc@ \
	@nspr_inc@ @svrcore_inc@
ns_slapd_LDADD = libslapd.la libldaputil.a $(LDAPSDK_LINK) $(NSS_LINK) \
	$(NSPR_LINK) $(SASL_LINK) $(SVRCORE_LINK) $(LIBNSL) $(LIBSOCKET)
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

pwdhash_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
pwdhash_bin_LDADD = libslapd.la $(NSPR_LINK) $(NSS_LINK) $(SVRCORE_LINK) $(LDAPSDK_LINK) $(SASL_LINK)

#------------------------
# rsearch
#------------------------
rsearch_bin_SOURCES = ldap/servers/slapd/tools/rsearch/nametable.c \
	ldap/servers/slapd/tools/rsearch/rsearch.c \
	ldap/servers/slapd/tools/rsearch/sdattable.c \
	ldap/servers/slapd/tools/rsearch/searchthread.c

rsearch_bin_CPPFLAGS = $(AM_CPPFLAGS) @ldapsdk_inc@ @nss_inc@ @nspr_inc@
rsearch_bin_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBSOCKET)

# these are for the config files and scripts that we need to generate and replace
# the paths and other tokens with the real values set during configure/make
# note that we cannot just use AC_OUTPUT to do this for us, since it will do things like this:
# LD_LIBRARY_PATH = ${prefix}/lib/fedora-ds
# i.e. it literally copies in '${prefix}' rather than expanding it out - we want this instead:
# LD_LIBRARY_PATH = /usr/lib/fedora-ds
if BUNDLE
fixupcmd = sed \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@nspr_libdir\@,$(libdir),g' \
	-e 's,@nss_libdir\@,$(libdir),g' \
	-e 's,@ldapsdk_libdir\@,$(libdir),g' \
	-e 's,@ldapsdk_bindir\@,$(bindir),g' \
	-e 's,@db_libdir\@,$(libdir),g' \
	-e 's,@db_bindir\@,$(bindir),g' \
	-e 's,@sasl_libdir\@,$(libdir),g' \
	-e 's,@netsnmp_libdir\@,$(libdir),g' \
	-e 's,@propertydir\@,$(propertydir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@schemadir\@,$(schemadir),g' \
	-e 's,@serverdir\@,$(serverdir),g' \
	-e 's,@serverplugindir\@,$(serverplugindir),g' \
	-e 's,@taskdir\@,$(taskdir),g' \
	-e 's,@configdir\@,$(configdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@package_name\@,$(PACKAGE_NAME),g' \
	-e 's,@instconfigdir\@,$(instconfigdir),g' \
	-e 's,@ECHO_N\@,$(ECHO_N),g' \
	-e 's,@ECHO_C\@,$(ECHO_C),g'
else
fixupcmd = sed \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@nspr_libdir\@,$(nspr_libdir),g' \
	-e 's,@nss_libdir\@,$(nss_libdir),g' \
	-e 's,@ldapsdk_libdir\@,$(ldapsdk_libdir),g' \
	-e 's,@ldapsdk_bindir\@,$(ldapsdk_bindir),g' \
	-e 's,@db_libdir\@,$(db_libdir),g' \
	-e 's,@db_bindir\@,$(db_bindir),g' \
	-e 's,@sasl_libdir\@,$(sasl_libdir),g' \
	-e 's,@netsnmp_libdir\@,$(netsnmp_libdir),g' \
	-e 's,@propertydir\@,$(propertydir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@schemadir\@,$(schemadir),g' \
	-e 's,@serverdir\@,$(serverdir),g' \
	-e 's,@serverplugindir\@,$(serverplugindir),g' \
	-e 's,@taskdir\@,$(taskdir),g' \
	-e 's,@configdir\@,$(configdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@package_name\@,$(PACKAGE_NAME),g' \
	-e 's,@instconfigdir\@,$(instconfigdir),g' \
	-e 's,@ECHO_N\@,$(ECHO_N),g' \
	-e 's,@ECHO_C\@,$(ECHO_C),g'
endif

%: %.in
	mkdir -p $(dir $@)
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME): %/initscript.in
	$(fixupcmd) $^ > $@
