# look for included m4 files in the ./m4/ directory
ACLOCAL_AMFLAGS = -I m4
NULLSTRING :=
SPACE := $(NULLSTRING) # the space is between the ) and the #
COLON := $(NULLSTRING):# a colon
QUOTE := $(NULLSTRING)"# a double quote"

#------------------------
# Compiler Flags
#------------------------
#
# First, we setup the definitions from configure.ac
#

PYTHON := python3
if DEBUG
# This allows sccache to work correctly with C files.
BUILDNUM := "\"0000.000.0000\""
else
BUILDNUM := $(shell $(srcdir)/buildnum.py)
endif
NQBUILDNUM := $(subst \,,$(subst $(QUOTE),,$(BUILDNUM)))
DEBUG_DEFINES = @debug_defs@
DEBUG_CFLAGS = @debug_cflags@
DEBUG_CXXFLAGS = @debug_cxxflags@
GCCSEC_CFLAGS = @gccsec_cflags@
if CLANG_ENABLE
ASAN_CFLAGS = @asan_cflags@
else
if enable_asan
ASAN_CFLAGS = @asan_cflags@ -lasan
else
ASAN_CFLAGS = @asan_cflags@
endif
endif
MSAN_CFLAGS = @msan_cflags@
TSAN_CFLAGS = @tsan_cflags@
UBSAN_CFLAGS = @ubsan_cflags@

if CFI_ENABLE
# https://clang.llvm.org/docs/ControlFlowIntegrity.html#available-schemes
# vcall is "forward edge" cfi which is what gives a lot of benefit security wise.
CFI_CFLAGS = -flto=thin -fsanitize=cfi-cast-strict,cfi-vcall -fvisibility=hidden
# Settings we could use in the future
# -fsanitize=cfi-icall,cfi-nvcall,cfi-derived-cast,cfi-unrelated-cast,cfi-mfcall
else
CFI_CFLAGS =
endif

SYSTEMD_DEFINES = @systemd_defs@

CMOCKA_INCLUDES = $(CMOCKA_CFLAGS)

PROFILING_DEFINES = @profiling_defs@
SYSTEMTAP_DEFINES = @systemtap_defs@
NSPR_INCLUDES = $(NSPR_CFLAGS)

# Rust inclusions.
CARGO_FLAGS = @cargo_defs@

if CLANG_ENABLE
RUSTC_FLAGS = @asan_rust_defs@ @msan_rust_defs@ @tsan_rust_defs@ @debug_rust_defs@
RUSTC_LINK_FLAGS = -C link-arg=-fuse-ld=lld
RUST_LDFLAGS = -ldl -lpthread -lc -lm -lrt -lutil
else
RUSTC_FLAGS = @asan_rust_defs@ @msan_rust_defs@ @tsan_rust_defs@ @debug_rust_defs@
RUSTC_LINK_FLAGS =
# This avoids issues with stderr being double provided with clang + asan.
RUST_LDFLAGS = -ldl -lpthread -lgcc_s -lc -lm -lrt -lutil
endif
RUST_DEFINES = -DRUST_ENABLE
if RUST_ENABLE_OFFLINE
RUST_OFFLINE = --locked --offline
else
RUST_OFFLINE =
endif

if CLANG_ENABLE
CLANG_ON = 1
CLANG_LDFLAGS = -latomic -fuse-ld=lld
EXPORT_LDFLAGS =
else
CLANG_ON = 0
CLANG_LDFLAGS =
if DEBUG
EXPORT_LDFLAGS = -rdynamic
endif
endif

REWRITERS_INCLUDES = -I$(srcdir)/src/rewriters/

SVRCORE_INCLUDES = -I$(srcdir)/src/svrcore/src/

if CLANG_ENABLE
# clang complains about the -U.
DS_DEFINES = -DBUILD_NUM=$(BUILDNUM) -DVENDOR="\"$(vendor)\"" -DBRAND="\"$(brand)\"" -DCAPBRAND="\"$(capbrand)\""
else
# the -U undefines these symbols - should use the corresponding DS_ ones instead - see configure.ac
DS_DEFINES = -DBUILD_NUM=$(BUILDNUM) -DVENDOR="\"$(vendor)\"" -DBRAND="\"$(brand)\"" -DCAPBRAND="\"$(capbrand)\"" \
	-UPACKAGE_VERSION -UPACKAGE_TARNAME -UPACKAGE_STRING -UPACKAGE_BUGREPORT
endif
DS_INCLUDES = -I$(srcdir)/ldap/include -I$(srcdir)/ldap/servers/slapd -I$(srcdir)/include -I.


if enable_asan
ASAN_ON = 1
SANITIZER = ASAN
else
ASAN_ON = 0
endif

if enable_msan
MSAN_ON = 1
SANITIZER = MSAN
else
MSAN_ON = 0
endif

if enable_tsan
TSAN_ON = 1
SANITIZER = TSAN
else
TSAN_ON = 0
endif

if enable_ubsan
UBSAN_ON = 1
SANITIZER = UBSAN
else
UBSAN_ON = 0
endif

if with_systemd
WITH_SYSTEMD = 1
else
WITH_SYSTEMD = 0
endif

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
	-DSBINDIR="\"$(sbindir)\"" -DPLUGINDIR="\"$(serverplugindir)\"" \
	-DTEMPLATEDIR="\"$(sampledatadir)\"" -DSYSTEMSCHEMADIR="\"$(systemschemadir)\"" \
	-DLOCALRUNDIR="\"$(localrundir)\""
# Now that we have all our defines in place, setup the CPPFLAGS

# These flags are the "must have" for all components
AM_CPPFLAGS = $(DEBUG_DEFINES) $(PROFILING_DEFINES) $(SYSTEMTAP_DEFINES) $(RUST_DEFINES)
AM_CFLAGS = $(DEBUG_CFLAGS) $(GCCSEC_CFLAGS) $(ASAN_CFLAGS) $(MSAN_CFLAGS) $(TSAN_CFLAGS) $(UBSAN_CFLAGS)
AM_CXXFLAGS = $(DEBUG_CXXFLAGS) $(GCCSEC_CFLAGS) $(ASAN_CFLAGS) $(MSAN_CFLAGS) $(TSAN_CFLAGS) $(UBSAN_CFLAGS)
# Flags for Directory Server
# WARNING: This needs a clean up, because slap.h is a horrible mess and is publically exposed!
DSPLUGIN_CPPFLAGS = $(DS_DEFINES) $(DS_INCLUDES) $(PATH_DEFINES) $(SYSTEMD_DEFINES) @openldap_inc@ $(NSS_CFLAGS) $(NSPR_INCLUDES) $(SYSTEMD_CFLAGS)
# This should give access to internal headers only for tests!!!
DSINTERNAL_CPPFLAGS = -I$(srcdir)/include/ldaputil
# Flags for Datastructure Library

#------------------------
# Linker Flags
#------------------------
CMOCKA_LINKS = $(CMOCKA_LIBS)
PROFILING_LINKS = @profiling_links@

NSPR_LINK = $(NSPR_LIBS)
NSS_LINK = $(NSS_LIBS)

# OpenLDAP 2.5 and newer versions don't have libldap_r shared library (only libldap)
# For the older versions we should compile with libldap_r
if WITH_LIBLDAP_R
LDAPSDK_LINK = @openldap_lib@ -lldap_r@ol_libver@ @ldap_lib_ldif@ -llber@ol_libver@
else
LDAPSDK_LINK = @openldap_lib@ -lldap@ol_libver@ @ldap_lib_ldif@ -llber@ol_libver@
endif
ldaplib = @ldaplib@
ldaplib_defs = @ldaplib_defs@

DB_LINK = @db_lib@ -ldb-@db_libver@ -llmdb
DB_INC = @db_inc@
DB_IMPL = libback-ldbm.la
SASL_LINK = $(SASL_LIBS)
NETSNMP_LINK = @netsnmp_lib@ @netsnmp_link@
PAM_LINK = -lpam
EVENT_LINK = $(EVENT_LIBS)
PW_CRACK_LINK = -lcrack
ZLIB_LINK = -lz
JSON_C_LINK = -ljson-c

LIBSOCKET=@LIBSOCKET@
LIBNSL=@LIBNSL@
LIBDL=@LIBDL@
LIBCSTD=@LIBCSTD@
LIBCRUN=@LIBCRUN@
THREADLIB=@THREADLIB@
LIBCRYPT=@LIBCRYPT@

# We need to make sure that libpthread is linked before libc on HP-UX.
if HPUX
AM_LDFLAGS = -lpthread
else
#AM_LDFLAGS = -Wl,-z,defs
AM_LDFLAGS = $(ZLIB_LINK) $(JSON_C_LINK) $(PW_CRACK_LINK) $(RUST_LDFLAGS) $(ASAN_CFLAGS) $(MSAN_CFLAGS) $(TSAN_CFLAGS) $(UBSAN_CFLAGS) $(PROFILING_LINKS) $(CLANG_LDFLAGS) $(EXPORT_LDFLAGS)
endif #end hpux

# https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html#Updating-version-info
# So, libtool library versions are described by three integers:
#
# current
#
#     The most recent interface number that this library implements.
# revision
#
#     The implementation number of the current interface.
# age
#
#     The difference between the newest and oldest interfaces that this library implements. In other words, the library implements all the interface numbers in the range from number current - age to current.
#
# Here are a set of rules to help you update your library version information:
#
#    Start with version information of ‘0:0:0’ for each libtool library.
#    Update the version information only immediately before a public release of your software. More frequent updates are unnecessary, and only guarantee that the current interface number gets larger faster.
#    If the library source code has changed at all since the last update, then increment revision (‘c:r:a’ becomes ‘c:r+1:a’).
#    If any interfaces have been added, removed, or changed since the last update, increment current, and set revision to 0.
#    If any interfaces have been added since the last public release, then increment age.
#    If any interfaces have been removed or changed since the last public release, then set age to 0. 

SLAPD_LDFLAGS = -version-info 1:0:1


#------------------------
# Generated Sources
#------------------------
BUILT_SOURCES = dberrstrs.h rust-slapi-private.h rust-nsslapd-private.h \
	$(POLICY_FC)

if enable_posix_winsync
LIBPOSIX_WINSYNC_PLUGIN = libposix-winsync-plugin.la
endif

CLEANFILES =  dberrstrs.h ns-slapd.properties \
	ldap/admin/src/template-initconfig \
	ldap/ldif/template-baseacis.ldif ldap/ldif/template-bitwise.ldif ldap/ldif/template-country.ldif \
	ldap/ldif/template-dnaplugin.ldif ldap/ldif/template-domain.ldif ldap/ldif/template-dse.ldif \
	ldap/ldif/template-dse-minimal.ldif \
	ldap/ldif/template-ldapi-autobind.ldif ldap/ldif/template-ldapi-default.ldif \
	ldap/ldif/template-ldapi.ldif ldap/ldif/template-locality.ldif ldap/ldif/template-org.ldif \
	ldap/ldif/template-orgunit.ldif ldap/ldif/template-pampta.ldif ldap/ldif/template-sasl.ldif \
	ldap/ldif/template-state.ldif ldap/ldif/template-suffix-db.ldif \
	doxyfile.stamp rust-slapi-private.h\
	$(NULL)

clean-local:
	-rm -rf dist
	-rm -rf $(abs_top_builddir)/html
	-rm -rf $(abs_top_builddir)/man/man3
	-rm -rf $(abs_top_builddir)/rs

dberrstrs.h: Makefile $(srcdir)/ldap/servers/slapd/mkDBErrStrs.py  $(srcdir)/ldap/servers/slapd/back-ldbm/dbimpl.h
	$(srcdir)/ldap/servers/slapd/mkDBErrStrs.py -i $(srcdir)/ldap/servers/slapd/back-ldbm -o .


#------------------------
# Install Paths
#------------------------
prefixdir = @prefixdir@
configdir = $(sysconfdir)@configdir@
sampledatadir = $(datadir)@sampledatadir@
systemschemadir = $(datadir)@systemschemadir@
propertydir = $(datadir)@propertydir@
schemadir = $(sysconfdir)@schemadir@
serverdir = $(libdir)/@serverdir@
serverplugindir = $(libdir)@serverplugindir@
taskdir = $(datadir)@scripttemplatedir@
systemdsystemunitdir = @with_systemdsystemunitdir@
systemdsystemunitdropindir = @with_systemdsystemunitdir@/$(PACKAGE_NAME)@.service.d
systemdsystemconfdir = @with_systemdsystemconfdir@
systemdgroupname = @with_systemdgroupname@
initdir = @initdir@
initconfigdir = $(sysconfdir)@initconfigdir@
instconfigdir = @instconfigdir@
perldir = $(libdir)@perldir@
pythondir = $(libdir)@pythondir@
infdir = $(datadir)@infdir@
mibdir = $(datadir)@mibdir@
updatedir = $(datadir)@updatedir@
pkgconfigdir = $(libdir)/pkgconfig
serverincdir = $(includedir)/@serverincdir@
gdbautoloaddir = $(prefixdir)/share/gdb/auto-load$(sbindir)
cockpitdir = $(prefixdir)/share/cockpit@cockpitdir@
metainfodir = $(prefixdir)/share/metainfo/389-console
tmpfiles_d = @tmpfiles_d@

# This has to be hardcoded to /lib - $libdir changes between lib/lib64, but
# sysctl.d is always in /lib.
sysctldir = @prefixdir@/lib/sysctl.d

defaultuser=@defaultuser@
defaultgroup=@defaultgroup@

#------------------------
# Build Products
#------------------------
sbin_PROGRAMS = ns-slapd ldap-agent

bin_PROGRAMS = dbscan \
	ldclt \
	pwdhash

# ----------------------------------------------------------------------------------------
# This odd looking definition is to keep the libraries in ORDER that they are needed. rsds
# is needed by sds, which is needed by ns. So we have a blank LTLIB, then append in order
# based on defines
# ----------------------------------------------------------------------------------------

server_LTLIBRARIES = libslapd.la libldaputil.la libns-dshttpd.la librewriters.la

lib_LTLIBRARIES = libsvrcore.la

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

if enable_acctpolicy
LIBACCTPOLICY_PLUGIN = libacctpolicy-plugin.la
LIBACCTPOLICY_SCHEMA = $(srcdir)/ldap/schema/60acctpolicy.ldif
enable_acctpolicy = 1
endif

serverplugin_LTLIBRARIES = libacl-plugin.la \
	libaddn-plugin.la \
	libattr-unique-plugin.la \
	libautomember-plugin.la libback-ldbm.la libchainingdb-plugin.la \
	libcollation-plugin.la libcos-plugin.la libderef-plugin.la \
	libpbe-plugin.la libdistrib-plugin.la \
	liblinkedattrs-plugin.la libmanagedentries-plugin.la \
	libmemberof-plugin.la libpassthru-plugin.la libpwdstorage-plugin.la \
	libcontentsync-plugin.la \
	libreferint-plugin.la libreplication-plugin.la libretrocl-plugin.la \
	libroles-plugin.la libstatechange-plugin.la libsyntax-plugin.la \
	libviews-plugin.la libschemareload-plugin.la libusn-plugin.la \
	libacctusability-plugin.la librootdn-access-plugin.la \
	libwhoami-plugin.la libalias-entries-plugin.la $(LIBACCTPOLICY_PLUGIN) \
	$(LIBPAM_PASSTHRU_PLUGIN) $(LIBDNA_PLUGIN) \
	$(LIBBITWISE_PLUGIN) $(LIBPRESENCE_PLUGIN) $(LIBPOSIX_WINSYNC_PLUGIN) \
	libentryuuid-plugin.la libentryuuid-syntax-plugin.la libpwdchan-plugin.la

noinst_LIBRARIES = libavl.a

dist_noinst_HEADERS = \
	include/i18n.h \
	include/netsite.h \
	include/base/crit.h \
	include/base/dbtbase.h \
	include/base/ereport.h \
	include/base/file.h \
	include/base/fsmutex.h \
	include/base/plist.h \
	include/base/pool.h \
	include/base/shexp.h \
	include/base/systems.h \
	include/base/systhr.h \
	include/base/util.h \
	include/ldaputil/cert.h \
	include/ldaputil/certmap.h \
	include/ldaputil/dbconf.h \
	include/ldaputil/encode.h \
	include/ldaputil/errors.h \
	include/ldaputil/init.h \
	include/ldaputil/ldapauth.h \
	include/ldaputil/ldaputil.h \
	include/libaccess/aclerror.h \
	include/libaccess/acleval.h \
	include/libaccess/aclglobal.h \
	include/libaccess/acl.h \
	include/libaccess/aclproto.h \
	include/libaccess/aclstruct.h \
	include/libaccess/attrec.h \
	include/libaccess/authdb.h \
	include/libaccess/dbtlibaccess.h \
	include/libaccess/dnfstruct.h \
	include/libaccess/ipfstruct.h \
	include/libaccess/las.h \
	include/libaccess/nsautherr.h \
	include/libaccess/nsauth.h \
	include/libaccess/nserror.h \
	include/libaccess/symbols.h \
	include/libaccess/userauth.h \
	include/libaccess/usi.h \
	include/libaccess/usrcache.h \
	include/libadmin/dbtlibadmin.h \
	include/libadmin/libadmin.h \
	include/public/netsite.h \
	include/public/nsapi.h \
	include/public/base/systems.h \
	include/public/nsacl/aclapi.h \
	include/public/nsacl/acldef.h \
	include/public/nsacl/nserrdef.h \
	include/public/nsacl/plistdef.h \
	ldap/include/avl.h \
	ldap/include/dblayer.h \
	ldap/include/disptmpl.h \
	ldap/include/ldaprot.h \
	ldap/include/portable.h \
	ldap/include/regex.h \
	ldap/include/srchpref.h \
	ldap/include/sysexits-compat.h \
	ldap/servers/plugins/addn/addn.h \
	ldap/servers/plugins/collation/config.h \
	ldap/servers/plugins/collation/collate.h \
	ldap/servers/plugins/collation/orfilter.h \
	ldap/servers/plugins/chainingdb/cb.h \
	ldap/servers/plugins/deref/deref.h \
	ldap/servers/plugins/acctpolicy/acctpolicy.h \
	ldap/servers/plugins/posix-winsync/posix-wsp-ident.h \
	ldap/servers/plugins/posix-winsync/posix-group-func.h \
	ldap/servers/plugins/roles/roles_cache.h \
	ldap/servers/plugins/usn/usn.h \
	ldap/servers/plugins/pwdstorage/pwdstorage.h \
	ldap/servers/plugins/pwdstorage/md5.h \
	ldap/servers/plugins/acl/acl.h \
	ldap/servers/plugins/linkedattrs/linked_attrs.h \
	ldap/servers/plugins/rootdn_access/rootdn_access.h \
	ldap/servers/plugins/acct_usability/acct_usability.h \
	ldap/servers/plugins/retrocl/retrocl.h \
	ldap/servers/plugins/uiduniq/plugin-utils.h \
	ldap/servers/plugins/memberof/memberof.h \
	ldap/servers/plugins/replication/cl5_api.h \
	ldap/servers/plugins/replication/llist.h \
	ldap/servers/plugins/replication/repl_shared.h \
	ldap/servers/plugins/replication/csnpl.h \
	ldap/servers/plugins/replication/cl5.h \
	ldap/servers/plugins/replication/repl-session-plugin.h \
	ldap/servers/plugins/replication/windows_prot_private.h \
	ldap/servers/plugins/replication/repl_helper.h \
	ldap/servers/plugins/replication/repl5.h \
	ldap/servers/plugins/replication/cl5_test.h \
	ldap/servers/plugins/replication/repl5_ruv.h \
	ldap/servers/plugins/replication/cl5_clcache.h \
	ldap/servers/plugins/replication/cl_crypt.h \
	ldap/servers/plugins/replication/urp.h \
	ldap/servers/plugins/replication/winsync-plugin.h \
	ldap/servers/plugins/replication/windowsrepl.h \
	ldap/servers/plugins/replication/repl5_prot_private.h \
	ldap/servers/plugins/pam_passthru/pam_passthru.h \
	ldap/servers/plugins/syntaxes/syntax.h \
	ldap/servers/plugins/cos/cos_cache.h \
	ldap/servers/plugins/sync/sync.h \
	ldap/servers/plugins/passthru/passthru.h \
	ldap/servers/plugins/rever/rever.h \
	ldap/servers/plugins/automember/automember.h \
	ldap/servers/plugins/alias_entries/alias-entries.h \
	ldap/servers/plugins/mep/mep.h \
	ldap/servers/slapd/agtmmap.h \
	ldap/servers/slapd/auth.h \
	ldap/servers/slapd/csngen.h \
	ldap/servers/slapd/disconnect_errors.h \
	ldap/servers/slapd/disconnect_error_strings.h \
	ldap/servers/slapd/fe.h \
	ldap/servers/slapd/filter.h \
	ldap/servers/slapd/getopt_ext.h \
	ldap/servers/slapd/getsocketpeer.h \
	ldap/servers/slapd/intrinsics.h \
	ldap/servers/slapd/log.h \
	ldap/servers/slapd/openldapber.h \
	ldap/servers/slapd/pblock_v3.h \
	ldap/servers/slapd/poll_using_select.h \
	ldap/servers/slapd/prerrstrs.h \
	ldap/servers/slapd/protect_db.h \
	ldap/servers/slapd/proto-slap.h \
	ldap/servers/slapd/pw.h \
	ldap/servers/slapd/pw_verify.h \
	ldap/servers/slapd/secerrstrs.h \
	ldap/servers/slapd/slap.h \
	ldap/servers/slapd/slapi_pal.h \
	ldap/servers/slapd/slapi-plugin-compat4.h \
	ldap/servers/slapd/slapi-plugin.h \
	ldap/servers/slapd/slapi-private.h \
	ldap/servers/slapd/snmp_collator.h \
	ldap/servers/slapd/sslerrstrs.h \
	ldap/servers/slapd/statechange.h \
	ldap/servers/slapd/uuid.h \
	ldap/servers/slapd/vattr_spi.h \
	ldap/servers/slapd/views.h \
	ldap/servers/slapd/back-ldbm/attrcrypt.h \
	ldap/servers/slapd/back-ldbm/back-ldbm.h \
	ldap/servers/slapd/back-ldbm/dbimpl.h \
	ldap/servers/slapd/back-ldbm/dblayer.h \
	ldap/servers/slapd/back-ldbm/import.h \
	ldap/servers/slapd/back-ldbm/ldbm_config.h \
	ldap/servers/slapd/back-ldbm/proto-back-ldbm.h \
	ldap/servers/slapd/back-ldbm/vlv_key.h \
	ldap/servers/slapd/back-ldbm/vlv_srch.h \
	ldap/servers/slapd/tools/ldaptool.h \
	ldap/servers/slapd/tools/ldaptool-sasl.h \
	ldap/servers/slapd/tools/ldclt/ldap-private.h \
	ldap/servers/slapd/tools/ldclt/ldclt.h \
	ldap/servers/slapd/tools/ldclt/port.h \
	ldap/servers/slapd/tools/ldclt/remote.h \
	ldap/servers/slapd/tools/ldclt/scalab01.h \
	ldap/servers/slapd/tools/ldclt/utils.h \
	ldap/servers/snmp/ldap-agent.h \
	ldap/systools/pio.h \
	lib/base/lexer_pvt.h \
	lib/base/plist_pvt.h \
	lib/ldaputil/ldaputili.h \
	lib/libaccess/access_plhash.h \
	lib/libaccess/aclcache.h \
	lib/libaccess/aclpriv.h \
	lib/libaccess/aclscan.h \
	lib/libaccess/acl.tab.h \
	lib/libaccess/aclutil.h \
	lib/libaccess/lasdns.h \
	lib/libaccess/las.h \
	lib/libaccess/lasip.h \
	lib/libaccess/ldapauth.h \
	lib/libaccess/oneeval.h \
	lib/libaccess/parse.h \
	lib/libaccess/permhash.h \
	lib/libsi18n/getstrmem.h \
	lib/libsi18n/gsslapd.h \
	lib/libsi18n/reshash.h \
	lib/libsi18n/txtfile.h

if ENABLE_CMOCKA
dist_noinst_HEADERS += \
	test/test_slapd.h
endif

dist_noinst_DATA = \
	$(srcdir)/buildnum.py \
	$(srcdir)/ldap/admin/src/*.in \
	$(srcdir)/ldap/admin/src/scripts/*.py \
	$(srcdir)/ldap/admin/src/scripts/ds-replcheck \
	$(srcdir)/ldap/ldif/*.in \
	$(srcdir)/ldap/ldif/*.ldif \
	$(srcdir)/ldap/schema/*.ldif \
	$(srcdir)/ldap/schema/slapd-collations.conf \
	$(srcdir)/ldap/servers/snmp/ldap-agent.conf \
	$(srcdir)/ldap/servers/snmp/redhat-directory.mib \
	$(srcdir)/ldap/servers/slapd/mkDBErrStrs.py \
	$(srcdir)/lib/ldaputil/certmap.conf \
	$(srcdir)/m4 \
	$(srcdir)/rpm/389-ds-base.spec.in \
	$(srcdir)/rpm/389-ds-base-devel.README \
	$(srcdir)/rpm/389-ds-base-git.sh \
	$(srcdir)/README.md \
	$(srcdir)/LICENSE \
	$(srcdir)/LICENSE.* \
	$(srcdir)/VERSION.sh \
	$(srcdir)/wrappers/*.in \
	$(srcdir)/dirsrvtests \
	$(srcdir)/src/lib389/setup.py \
	$(srcdir)/src/lib389


#------------------------
# Installed Files
#------------------------
config_DATA = $(srcdir)/lib/ldaputil/certmap.conf \
	$(srcdir)/ldap/schema/slapd-collations.conf \
	ldap/servers/snmp/ldap-agent.conf

# the schema files in this list are either not
# standard schema, not tested, or not compatible
# with the default schema e.g. there is
# considerable overlap of 60changelog.ldif and 01common.ldif
# and 60inetmail.ldif and 50ns-mail.ldif among others
sampledata_DATA = $(srcdir)/ldap/ldif/Ace.ldif \
	$(srcdir)/ldap/ldif/European.ldif \
	$(srcdir)/ldap/ldif/Eurosuffix.ldif \
	$(srcdir)/ldap/ldif/Example.ldif \
	$(srcdir)/ldap/ldif/Example-roles.ldif \
	$(srcdir)/ldap/ldif/Example-views.ldif \
	$(srcdir)/ldap/ldif/template.ldif \
	ldap/ldif/template-dse.ldif \
	ldap/ldif/template-dse-minimal.ldif \
	ldap/ldif/template-suffix-db.ldif \
	ldap/ldif/template-ldapi.ldif \
	ldap/ldif/template-ldapi-default.ldif \
	ldap/ldif/template-ldapi-autobind.ldif \
	ldap/ldif/template-org.ldif \
	ldap/ldif/template-domain.ldif \
	ldap/ldif/template-state.ldif \
	ldap/ldif/template-locality.ldif \
	ldap/ldif/template-country.ldif \
	ldap/ldif/template-orgunit.ldif \
	ldap/ldif/template-baseacis.ldif \
	ldap/ldif/template-sasl.ldif \
	$(srcdir)/ldap/schema/10rfc2307compat.ldif \
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
	$(srcdir)/ldap/schema/60sendmail.ldif \
	$(srcdir)/ldap/schema/dsee.schema \
	$(srcdir)/src/lib389/lib389/cli_ctl/dbgen-FamilyNames \
	$(srcdir)/src/lib389/lib389/cli_ctl/dbgen-GivenNames \
	$(srcdir)/src/lib389/lib389/cli_ctl/dbgen-OrgUnits \
	$(LIBPRESENCE_SCHEMA)

systemschema_DATA = $(srcdir)/ldap/schema/00core.ldif \
	$(srcdir)/ldap/schema/01core389.ldif \
	$(srcdir)/ldap/schema/02common.ldif \
	$(srcdir)/ldap/schema/05rfc2927.ldif \
	$(srcdir)/ldap/schema/05rfc4523.ldif \
	$(srcdir)/ldap/schema/05rfc4524.ldif \
	$(srcdir)/ldap/schema/06inetorgperson.ldif \
	$(srcdir)/ldap/schema/10automember-plugin.ldif \
	$(srcdir)/ldap/schema/10dna-plugin.ldif \
	$(srcdir)/ldap/schema/10mep-plugin.ldif \
	$(srcdir)/ldap/schema/10rfc2307compat.ldif \
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
	$(srcdir)/ldap/schema/60posix-winsync-plugin.ldif \
	$(srcdir)/ldap/schema/60autofs.ldif \
	$(srcdir)/ldap/schema/60eduperson.ldif \
	$(srcdir)/ldap/schema/60mozilla.ldif \
	$(srcdir)/ldap/schema/60pureftpd.ldif \
	$(srcdir)/ldap/schema/60rfc2739.ldif \
	$(srcdir)/ldap/schema/60rfc3712.ldif \
	$(srcdir)/ldap/schema/60sabayon.ldif \
	$(srcdir)/ldap/schema/60samba3.ldif \
	$(srcdir)/ldap/schema/60sudo.ldif \
	$(srcdir)/ldap/schema/60trust.ldif \
	$(srcdir)/ldap/schema/60nss-ldap.ldif \
	$(srcdir)/ldap/schema/03entryuuid.ldif \
	$(LIBACCTPOLICY_SCHEMA)

schema_DATA = $(srcdir)/ldap/schema/99user.ldif

libexec_SCRIPTS =

if SYSTEMD
libexec_SCRIPTS += wrappers/ds_systemd_ask_password_acl wrappers/ds_selinux_restorecon.sh
endif

if ENABLE_COCKPIT
install-data-hook:
	if [ "$(srcdir)" != "." ]; then cp -r $(srcdir)/src/cockpit src ; fi
	mkdir -p src/cockpit/389-console/cockpit_dist/
	mkdir -p $(DESTDIR)$(cockpitdir)
	rsync -rupE src/cockpit/389-console/cockpit_dist/ $(DESTDIR)$(cockpitdir)
	mkdir -p $(DESTDIR)$(metainfodir)
	rsync -up src/cockpit/389-console/org.port389.cockpit_console.metainfo.xml $(DESTDIR)$(metainfodir)/org.port389.cockpit_console.metainfo.xml
endif

sbin_SCRIPTS =

bin_SCRIPTS =

# For scripts that are "as is".
dist_bin_SCRIPTS = ldap/admin/src/scripts/ds-replcheck \
	ldap/admin/src/scripts/ds-logpipe.py

dist_bin_SCRIPTS += ldap/admin/src/logconv.pl

python_DATA = ldap/admin/src/scripts/failedbinds.py \
	ldap/admin/src/scripts/logregex.py

gdbautoload_DATA = ldap/admin/src/scripts/ns-slapd-gdb.py

dist_sysctl_DATA = ldap/admin/src/70-dirsrv.conf

if SYSTEMD
# yes, that is an @ in the filename . . .
systemdsystemunit_DATA = wrappers/$(PACKAGE_NAME)@.service \
	wrappers/$(systemdgroupname) \
	wrappers/$(PACKAGE_NAME)-snmp.service

if with_sanitizer
systemdsystemunitdropin_DATA = wrappers/$(PACKAGE_NAME)@.service.d/xsan.conf
else
systemdsystemunitdropin_DATA = wrappers/$(PACKAGE_NAME)@.service.d/custom.conf
endif

else
if INITDDIR
init_SCRIPTS = wrappers/$(PACKAGE_NAME) \
	wrappers/$(PACKAGE_NAME)-snmp
endif
endif

if INITDDIR
initconfig_DATA = ldap/admin/src/$(PACKAGE_NAME)
endif

inf_DATA = ldap/admin/src/slapd.inf \
	ldap/admin/src/defaults.inf

mib_DATA = ldap/servers/snmp/redhat-directory.mib

pkgconfig_DATA = src/pkgconfig/dirsrv.pc \
	src/pkgconfig/svrcore.pc

#------------------------
# header files
#------------------------
serverinc_HEADERS = ldap/servers/plugins/replication/repl-session-plugin.h \
	ldap/servers/slapd/slapi_pal.h \
	ldap/servers/slapd/slapi-plugin.h \
	ldap/servers/plugins/replication/winsync-plugin.h

include_HEADERS = src/svrcore/src/svrcore.h

#------------------------
# man pages
#------------------------
dist_man_MANS = man/man1/dbscan.1 \
	man/man1/ds-logpipe.py.1 \
	man/man1/ds-replcheck.1 \
	man/man1/ldap-agent.1 \
	man/man1/ldclt.1 \
	man/man1/logconv.pl.1 \
	man/man1/pwdhash.1 \
	man/man5/99user.ldif.5 \
	man/man8/ns-slapd.8 \
	man/man5/certmap.conf.5 \
	man/man5/dirsrv.5 \
	man/man5/dirsrv.systemd.5 \
	man/man5/slapd-collations.conf.5

#////////////////////////////////////////////////////////////////
#
#   Static Server Libraries
#
#////////////////////////////////////////////////////////////////
#------------------------
# libavl
#------------------------
libavl_a_SOURCES = ldap/libraries/libavl/avl.c
libavl_a_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)

#------------------------
# libldaputil
#------------------------
libldaputil_la_SOURCES = lib/ldaputil/cert.c \
	lib/ldaputil/certmap.c \
	lib/ldaputil/dbconf.c \
	lib/ldaputil/encode.c \
	lib/ldaputil/errors.c \
	lib/ldaputil/init.c \
	lib/ldaputil/ldapauth.c \
	lib/ldaputil/vtable.c

libldaputil_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(DSINTERNAL_CPPFLAGS) -I$(srcdir)/lib/ldaputil
libldaputil_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK)
libldaputil_la_LDFLAGS = $(AM_LDFLAGS)

#////////////////////////////////////////////////////////////////
#
#   Dynamic Server Libraries
#
#////////////////////////////////////////////////////////////////

#------------------------
# librewriters
#------------------------
librewriters_la_SOURCES = \
	src/rewriters/adfilter.c

librewriters_la_LDFLAGS = $(AM_LDFLAGS)
librewriters_la_CPPFLAGS = $(AM_CPPFLAGS) $(REWRITERS_INCLUDES) $(DSPLUGIN_CPPFLAGS)
librewriters_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK)

#------------------------
# libsvrcore
#------------------------
libsvrcore_la_SOURCES = \
	src/svrcore/src/alt.c \
	src/svrcore/src/cache.c \
	src/svrcore/src/errors.c \
	src/svrcore/src/file.c \
	src/svrcore/src/ntgetpin.c \
	src/svrcore/src/ntresource.h \
	src/svrcore/src/pin.c \
	src/svrcore/src/pk11.c \
	src/svrcore/src/std.c \
	src/svrcore/src/systemd-ask-pass.c \
	src/svrcore/src/std-systemd.c \
	src/svrcore/src/user.c

libsvrcore_la_LDFLAGS = $(AM_LDFLAGS)
libsvrcore_la_CPPFLAGS = $(AM_CPPFLAGS) $(SVRCORE_INCLUDES) $(DSPLUGIN_CPPFLAGS)
libsvrcore_la_LIBADD = $(NSS_LINK) $(NSPR_LINK)

noinst_LTLIBRARIES = librslapd.la librnsslapd.la libentryuuid.la libentryuuid_syntax.la \
	libpwdchan.la

### Why does this exist?
#
# Both cargo and autotools are really opinionated. It's really hard to make this work. :(
#
# https://people.gnome.org/~federico/blog/librsvg-build-infrastructure.html
# https://gitlab.gnome.org/GNOME/librsvg/blob/master/Makefile.am

### Rust lib slapd components
RSLAPD_LIB = @abs_top_builddir@/rs/rslapd/@rust_target_dir@/librslapd.a

librslapd_la_SOURCES = \
	src/librslapd/Cargo.toml \
	src/librslapd/build.rs \
	src/librslapd/src/cache.rs \
	src/librslapd/src/lib.rs

librslapd_la_EXTRA = src/librslapd/Cargo.lock

@abs_top_builddir@/rs/rslapd/@rust_target_dir@/librslapd.a: $(librslapd_la_SOURCES)
	RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
	CARGO_TARGET_DIR=$(abs_top_builddir)/rs/rslapd \
	SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
	SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
		cargo rustc $(RUST_OFFLINE) --manifest-path=$(srcdir)/src/librslapd/Cargo.toml \
		$(CARGO_FLAGS) --verbose -- $(RUSTC_FLAGS) $(RUSTC_LINK_FLAGS)

# The header needs the lib build first.
rust-slapi-private.h: @abs_top_builddir@/rs/rslapd/@rust_target_dir@/librslapd.a

# Build rust ns-slapd components as a library.
RNSSLAPD_LIB = @abs_top_builddir@/rs/rnsslapd/@rust_target_dir@/librnsslapd.a

librnsslapd_la_SOURCES = \
	src/librnsslapd/Cargo.toml \
	src/librnsslapd/build.rs \
	src/librnsslapd/src/lib.rs

librnsslapd_la_EXTRA = src/librnsslapd/Cargo.lock

@abs_top_builddir@/rs/rnsslapd/@rust_target_dir@/librnsslapd.a: $(librnsslapd_la_SOURCES)
	RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
	CARGO_TARGET_DIR=$(abs_top_builddir)/rs/rnsslapd \
	SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
	SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
		cargo rustc $(RUST_OFFLINE) --manifest-path=$(srcdir)/src/librnsslapd/Cargo.toml \
		$(CARGO_FLAGS) --verbose -- $(RUSTC_FLAGS) $(RUSTC_LINK_FLAGS)

# The header needs the lib build first.
rust-nsslapd-private.h: @abs_top_builddir@/rs/rnsslapd/@rust_target_dir@/librnsslapd.a

libslapi_r_plugin_SOURCES = \
	src/slapi_r_plugin/src/backend.rs \
	src/slapi_r_plugin/src/ber.rs \
	src/slapi_r_plugin/src/charray.rs \
	src/slapi_r_plugin/src/constants.rs \
	src/slapi_r_plugin/src/dn.rs \
	src/slapi_r_plugin/src/entry.rs \
	src/slapi_r_plugin/src/error.rs \
	src/slapi_r_plugin/src/log.rs \
	src/slapi_r_plugin/src/macros.rs \
	src/slapi_r_plugin/src/pblock.rs \
	src/slapi_r_plugin/src/plugin.rs \
	src/slapi_r_plugin/src/search.rs \
	src/slapi_r_plugin/src/syntax_plugin.rs \
	src/slapi_r_plugin/src/task.rs \
	src/slapi_r_plugin/src/value.rs \
	src/slapi_r_plugin/src/lib.rs

# Build rust ns-slapd components as a library.
ENTRYUUID_LIB = @abs_top_builddir@/rs/entryuuid/@rust_target_dir@/libentryuuid.a

libentryuuid_la_SOURCES = \
	src/plugins/entryuuid/Cargo.toml \
	src/plugins/entryuuid/src/lib.rs \
	$(libslapi_r_plugin_SOURCES)

libentryuuid_la_EXTRA = src/plugin/entryuuid/Cargo.lock

@abs_top_builddir@/rs/entryuuid/@rust_target_dir@/libentryuuid.a: $(libentryuuid_la_SOURCES) libslapd.la libentryuuid.la
	RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
	CARGO_TARGET_DIR=$(abs_top_builddir)/rs/entryuuid \
	SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
	SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
		cargo rustc $(RUST_OFFLINE) --manifest-path=$(srcdir)/src/plugins/entryuuid/Cargo.toml \
		$(CARGO_FLAGS) --verbose -- $(RUSTC_FLAGS) $(RUSTC_LINK_FLAGS)
	cp $(ENTRYUUID_LIB) @abs_top_builddir@/.libs/libentryuuid.a

ENTRYUUID_SYNTAX_LIB = @abs_top_builddir@/rs/entryuuid_syntax/@rust_target_dir@/libentryuuid_syntax.a

libentryuuid_syntax_la_SOURCES = \
	src/plugins/entryuuid_syntax/Cargo.toml \
	src/plugins/entryuuid_syntax/src/lib.rs \
	$(libslapi_r_plugin_SOURCES)

libentryuuid_syntax_la_EXTRA = src/plugin/entryuuid_syntax/Cargo.lock

@abs_top_builddir@/rs/entryuuid_syntax/@rust_target_dir@/libentryuuid_syntax.a: $(libentryuuid_syntax_la_SOURCES) libslapd.la libentryuuid_syntax.la
	RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
	CARGO_TARGET_DIR=$(abs_top_builddir)/rs/entryuuid_syntax \
	SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
	SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
		cargo rustc $(RUST_OFFLINE) --manifest-path=$(srcdir)/src/plugins/entryuuid_syntax/Cargo.toml \
		$(CARGO_FLAGS) --verbose -- $(RUSTC_FLAGS) $(RUSTC_LINK_FLAGS)
	cp $(ENTRYUUID_SYNTAX_LIB) @abs_top_builddir@/.libs/libentryuuid_syntax.a

# == pwdchan

PWDCHAN_LIB = @abs_top_builddir@/rs/pwdchan/@rust_target_dir@/libpwdchan.a

libpwdchan_la_SOURCES = \
	src/plugins/pwdchan/Cargo.toml \
	src/plugins/pwdchan/src/lib.rs \
	$(libslapi_r_plugin_SOURCES)

libpwdchan_la_EXTRA = src/plugin/pwdchan/Cargo.lock

@abs_top_builddir@/rs/pwdchan/@rust_target_dir@/libpwdchan.a: $(libpwdchan_la_SOURCES) libslapd.la libpwdchan.la
	RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
	CARGO_TARGET_DIR=$(abs_top_builddir)/rs/pwdchan \
	SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
	SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
		cargo rustc $(RUST_OFFLINE) --manifest-path=$(srcdir)/src/plugins/pwdchan/Cargo.toml \
		$(CARGO_FLAGS) --verbose -- $(RUSTC_FLAGS) $(RUSTC_LINK_FLAGS)
	cp $(PWDCHAN_LIB) @abs_top_builddir@/.libs/libpwdchan.a

# == pwdchan

EXTRA_DIST = $(librslapd_la_SOURCES) $(librslapd_la_EXTRA) \
			$(libentryuuid_la_SOURCES) $(libentryuuid_la_EXTRA) \
			$(libentryuuid_syntax_la_SOURCES) $(libentryuuid_syntax_la_EXTRA) \
			$(libpwdchan_la_SOURCES) $(libpwdchan_la_EXTRA) \
			$(librnsslapd_la_SOURCES) $(librnsslapd_la_EXTRA)

## Run rust tests
# cargo does not support offline tests :(
if RUST_ENABLE_OFFLINE
else
if enable_asan
# Distro rust tends not to have proper asan support w_ clang
else
check-local:
	for thing in "librslapd" "librnsslapd" ; do \
		echo \
		LD_LIBRARY_PATH=$(abs_top_builddir)/.libs \
		RUSTFLAGS="$(RUSTC_FLAGS)" \
		RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
		CARGO_TARGET_DIR=$(abs_top_builddir)/rs/test/$${thing} \
		SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
		SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
			cargo test $(RUST_OFFLINE) \
			--manifest-path=$(srcdir)/src/$${thing}/Cargo.toml -- --nocapture ; \
		LD_LIBRARY_PATH=$(abs_top_builddir)/.libs \
		RUSTFLAGS="$(RUSTC_FLAGS)" \
		RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
		CARGO_TARGET_DIR=$(abs_top_builddir)/rs/test/$${thing} \
		SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
		SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
			cargo test $(RUST_OFFLINE) \
			--manifest-path=$(srcdir)/src/$${thing}/Cargo.toml -- --nocapture ; \
	done
# Plugin tests are a little different
	for thing in "plugins/pwdchan" ; do \
		echo \
		LD_LIBRARY_PATH=$(abs_top_builddir)/.libs \
		RUSTFLAGS="$(RUSTC_FLAGS)" \
		RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
		CARGO_TARGET_DIR=$(abs_top_builddir)/rs/test/$${thing} \
		SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
		SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
			cargo test $(RUST_OFFLINE) --features=slapi_r_plugin/test_log_direct \
			--manifest-path=$(srcdir)/src/$${thing}/Cargo.toml -- --nocapture ; \
		LD_LIBRARY_PATH=$(abs_top_builddir)/.libs \
		RUSTFLAGS="$(RUSTC_FLAGS)" \
		RUST_BACKTRACE=1 RUSTC_BOOTSTRAP=1 \
		CARGO_TARGET_DIR=$(abs_top_builddir)/rs/test/$${thing} \
		SLAPD_DYLIB_DIR=$(abs_top_builddir)/ \
		SLAPD_HEADER_DIR=$(abs_top_builddir)/ \
			cargo test $(RUST_OFFLINE) --features=slapi_r_plugin/test_log_direct \
			--manifest-path=$(srcdir)/src/$${thing}/Cargo.toml -- --nocapture ; \
	done
endif
endif

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
	lib/base/dnsdmain.cpp \
	lib/base/ereport.cpp \
	lib/base/file.cpp \
	lib/base/fsmutex.cpp \
	lib/base/nscperror.c \
	lib/base/plist.cpp \
	lib/base/pool.cpp \
	lib/base/shexp.cpp \
	lib/base/system.cpp \
	lib/base/systhr.cpp \
	lib/base/util.cpp \
	lib/libsi18n/getstrprop.c \
	lib/libsi18n/reshash.c \
	lib/libsi18n/txtfile.c

libns_dshttpd_la_CPPFLAGS = -I$(srcdir)/include/base $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) -I$(srcdir)/lib/ldaputil
libns_dshttpd_la_LIBADD = libslapd.la libldaputil.la $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK)
if CLANG_ENABLE
# This avoids issues with stderr being double provided with clang + asan.
libns_dshttpd_la_LDFLAGS = $(AM_LDFLAGS) -static-libgcc
else
libns_dshttpd_la_LDFLAGS = $(AM_LDFLAGS)
endif

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
	ldap/servers/slapd/configdse.c \
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
	ldap/servers/slapd/eventq-deprecated.c \
	ldap/servers/slapd/factory.c \
	ldap/servers/slapd/features.c \
	ldap/servers/slapd/fileio.c \
	ldap/servers/slapd/filter.c \
	ldap/servers/slapd/filtercmp.c \
	ldap/servers/slapd/filterentry.c \
	ldap/servers/slapd/generation.c \
	ldap/servers/slapd/getfilelist.c \
	ldap/servers/slapd/ldapi.c \
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
	ldap/servers/slapd/object.c \
	ldap/servers/slapd/objset.c \
	ldap/servers/slapd/operation.c \
	ldap/servers/slapd/opshared.c \
	ldap/servers/slapd/pagedresults.c \
	ldap/servers/slapd/pblock.c \
	ldap/servers/slapd/plugin.c \
	ldap/servers/slapd/plugin_acl.c \
	ldap/servers/slapd/plugin_mmr.c \
	ldap/servers/slapd/plugin_internal_op.c \
	ldap/servers/slapd/plugin_mr.c \
	ldap/servers/slapd/plugin_role.c \
	ldap/servers/slapd/plugin_syntax.c \
	ldap/servers/slapd/protect_db.c \
	ldap/servers/slapd/proxyauth.c \
	ldap/servers/slapd/pw.c \
	ldap/servers/slapd/pw_retry.c \
	ldap/servers/slapd/rdn.c \
	ldap/servers/slapd/referral.c \
	ldap/servers/slapd/regex.c \
	ldap/servers/slapd/resourcelimit.c \
	ldap/servers/slapd/result.c \
	ldap/servers/slapd/rewriters.c \
	ldap/servers/slapd/sasl_map.c \
	ldap/servers/slapd/schema.c \
	ldap/servers/slapd/schemaparse.c \
	ldap/servers/slapd/security_wrappers.c \
	ldap/servers/slapd/slapd_plhash.c \
	ldap/servers/slapd/slapi_counter.c \
	ldap/servers/slapd/slapi2runtime.c \
	ldap/servers/slapd/snmp_collator.c \
	ldap/servers/slapd/sort.c \
	ldap/servers/slapd/ssl.c \
	ldap/servers/slapd/str2filter.c \
	ldap/servers/slapd/subentry.c \
	ldap/servers/slapd/task.c \
	ldap/servers/slapd/time.c \
	ldap/servers/slapd/thread_data.c \
	ldap/servers/slapd/uniqueid.c \
	ldap/servers/slapd/uniqueidgen.c \
	ldap/servers/slapd/upgrade.c \
	ldap/servers/slapd/utf8.c \
	ldap/servers/slapd/utf8compare.c \
	ldap/servers/slapd/util.c \
	ldap/servers/slapd/uuid.c \
	ldap/servers/slapd/value.c \
	ldap/servers/slapd/valueset.c \
	ldap/servers/slapd/vattr.c \
	ldap/servers/slapd/slapi_pal.c \
	src/libsds/external/csiphash/csiphash.c \
	$(GETSOCKETPEER) \
	$(libavl_a_SOURCES)

libslapd_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(SASL_CFLAGS) $(DB_INC) $(KERBEROS_CFLAGS) $(PCRE_CFLAGS) $(SVRCORE_INCLUDES)
libslapd_la_LIBADD = $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK) $(KERBEROS_LIBS) $(PCRE_LIBS) $(THREADLIB) $(SYSTEMD_LIBS) libsvrcore.la $(RSLAPD_LIB) $(OPENSSL_LIBS)
# If asan is enabled, it creates special libcrypt interceptors. However, they are
# detected by the first load of libasan at runtime, and what is in the linked lib
# so we need libcrypt to be present as soon as libasan is loaded for the interceptors
# to function. Since ns-slapd links libslapd, this is pulled at startup, which allows
# pwdstorage to be asan checked with libcrypt.
if enable_asan
libslapd_la_LIBADD += $(LIBCRYPT)
endif
libslapd_la_LDFLAGS = $(AM_LDFLAGS) $(SLAPD_LDFLAGS)

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
	ldap/servers/slapd/back-ldbm/dbimpl.c \
	ldap/servers/slapd/back-ldbm/dblayer.c \
	ldap/servers/slapd/back-ldbm/dbsize.c \
	ldap/servers/slapd/back-ldbm/dn2entry.c \
	ldap/servers/slapd/back-ldbm/entrystore.c \
	ldap/servers/slapd/back-ldbm/filterindex.c \
	ldap/servers/slapd/back-ldbm/findentry.c \
	ldap/servers/slapd/back-ldbm/haschildren.c \
	ldap/servers/slapd/back-ldbm/id2entry.c \
	ldap/servers/slapd/back-ldbm/idl.c \
	ldap/servers/slapd/back-ldbm/idl_shim.c \
	ldap/servers/slapd/back-ldbm/idl_new.c \
	ldap/servers/slapd/back-ldbm/idl_set.c \
	ldap/servers/slapd/back-ldbm/idl_common.c \
	ldap/servers/slapd/back-ldbm/import.c \
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
	ldap/servers/slapd/back-ldbm/nextid.c \
	ldap/servers/slapd/back-ldbm/parents.c \
	ldap/servers/slapd/back-ldbm/rmdb.c \
	ldap/servers/slapd/back-ldbm/seq.c \
	ldap/servers/slapd/back-ldbm/sort.c \
	ldap/servers/slapd/back-ldbm/start.c \
	ldap/servers/slapd/back-ldbm/uniqueid2entry.c \
	ldap/servers/slapd/back-ldbm/vlv.c \
	ldap/servers/slapd/back-ldbm/vlv_key.c \
	ldap/servers/slapd/back-ldbm/vlv_srch.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_config.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_instance_config.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_verify.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_layer.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_misc.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_perfctrs.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_upgrade.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_version.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_monitor.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_ldif2db.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_import.c \
	ldap/servers/slapd/back-ldbm/db-bdb/bdb_import_threads.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_config.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_debug.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_instance.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_instance_config.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_verify.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_txn.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_layer.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_misc.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_perfctrs.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_upgrade.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_monitor.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_ldif2db.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_import.c \
	ldap/servers/slapd/back-ldbm/db-mdb/mdb_import_threads.c



libback_ldbm_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(DB_INC)
libback_ldbm_la_DEPENDENCIES = libslapd.la
libback_ldbm_la_LIBADD = libslapd.la $(DB_LINK) $(LDAPSDK_LINK) $(NSPR_LINK)
libback_ldbm_la_LDFLAGS = -avoid-version

#------------------------
# libacctpolicy-plugin
#------------------------
libacctpolicy_plugin_la_SOURCES = ldap/servers/plugins/acctpolicy/acct_config.c \
	ldap/servers/plugins/acctpolicy/acct_init.c \
	ldap/servers/plugins/acctpolicy/acct_plugin.c \
	ldap/servers/plugins/acctpolicy/acct_util.c

libacctpolicy_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libacctpolicy_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libacctpolicy_plugin_la_DEPENDENCIES = libslapd.la
libacctpolicy_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libacctusability-plugin
#------------------------
libacctusability_plugin_la_SOURCES = ldap/servers/plugins/acct_usability/acct_usability.c

libacctusability_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libacctusability_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libacctusability_plugin_la_DEPENDENCIES = libslapd.la
libacctusability_plugin_la_LDFLAGS = -avoid-version

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

libacl_plugin_la_CPPFLAGS = -I$(srcdir)/include/libaccess $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libacl_plugin_la_DEPENDENCIES = libslapd.la libns-dshttpd.la
libacl_plugin_la_LIBADD = libslapd.la libns-dshttpd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(LIBCSTD) $(LIBCRUN)
libacl_plugin_la_LDFLAGS = -avoid-version
# libacl_plugin_la_LINK = $(CXXLINK) -avoid-version

#------------------------
# libalias-entries-plugin
#------------------------
libalias_entries_plugin_la_SOURCES = ldap/servers/plugins/alias_entries/alias-entries.c

libalias_entries_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libalias_entries_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libalias_entries_plugin_la_DEPENDENCIES = libslapd.la
libalias_entries_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libaddn-plugin
#------------------------
libaddn_plugin_la_SOURCES = ldap/servers/plugins/addn/addn.c

libaddn_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libaddn_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libaddn_plugin_la_DEPENDENCIES = libslapd.la
libaddn_plugin_la_LDFLAGS = -avoid-version

#------------------------
# librootdn-access-plugin
#------------------------
#
librootdn_access_plugin_la_SOURCES = ldap/servers/plugins/rootdn_access/rootdn_access.c

librootdn_access_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
librootdn_access_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
librootdn_access_plugin_la_DEPENDENCIES = libslapd.la
librootdn_access_plugin_la_LDFLAGS = -avoid-version


#------------------------
# libautomember-plugin
#------------------------
libautomember_plugin_la_SOURCES = ldap/servers/plugins/automember/automember.c

libautomember_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libautomember_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libautomember_plugin_la_DEPENDENCIES = libslapd.la
libautomember_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libattr-unique-plugin
#------------------------
libattr_unique_plugin_la_SOURCES = ldap/servers/plugins/uiduniq/7bit.c \
	ldap/servers/plugins/uiduniq/uid.c \
	ldap/servers/plugins/uiduniq/utils.c

libattr_unique_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libattr_unique_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libattr_unique_plugin_la_DEPENDENCIES = libslapd.la
libattr_unique_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libbitwise-plugin
#------------------------
libbitwise_plugin_la_SOURCES = ldap/servers/plugins/bitwise/bitwise.c

libbitwise_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libbitwise_plugin_la_LIBADD = libslapd.la
libbitwise_plugin_la_DEPENDENCIES = libslapd.la
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
	ldap/servers/plugins/chainingdb/cb_start.c \
	ldap/servers/plugins/chainingdb/cb_temp.c \
	ldap/servers/plugins/chainingdb/cb_test.c \
	ldap/servers/plugins/chainingdb/cb_unbind.c \
	ldap/servers/plugins/chainingdb/cb_utils.c 

libchainingdb_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libchainingdb_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libchainingdb_plugin_la_DEPENDENCIES = libslapd.la
libchainingdb_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libcollation-plugin
#------------------------
libcollation_plugin_la_SOURCES = ldap/servers/plugins/collation/collate.c \
        ldap/servers/plugins/collation/config.c \
        ldap/servers/plugins/collation/orfilter.c

libcollation_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(ICU_CFLAGS)
libcollation_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(ICU_LIBS) $(LIBCSTD) $(LIBCRUN)
libcollation_plugin_la_DEPENDENCIES = libslapd.la
libcollation_plugin_la_LDFLAGS = -avoid-version
# libcollation_plugin_la_LINK = $(CXXLINK) -avoid-version

#------------------------
# libcos-plugin
#------------------------
libcos_plugin_la_SOURCES = ldap/servers/plugins/cos/cos.c \
	ldap/servers/plugins/cos/cos_cache.c

libcos_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libcos_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libcos_plugin_la_DEPENDENCIES = libslapd.la
libcos_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libderef-plugin
#-----------------------
libderef_plugin_la_SOURCES = ldap/servers/plugins/deref/deref.c

libderef_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libderef_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libderef_plugin_la_DEPENDENCIES = libslapd.la
libderef_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libentryuuid-syntax-plugin
#-----------------------
libentryuuid_syntax_plugin_la_SOURCES = src/slapi_r_plugin/src/init.c
libentryuuid_syntax_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) -lentryuuid_syntax
libentryuuid_syntax_plugin_la_DEPENDENCIES = libslapd.la $(ENTRYUUID_SYNTAX_LIB)
libentryuuid_syntax_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libentryuuid-plugin
#-----------------------
libentryuuid_plugin_la_SOURCES = src/slapi_r_plugin/src/init.c
libentryuuid_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) -lentryuuid
libentryuuid_plugin_la_DEPENDENCIES = libslapd.la $(ENTRYUUID_LIB)
libentryuuid_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpwdchan-plugin
#-----------------------
libpwdchan_plugin_la_SOURCES = src/slapi_r_plugin/src/init.c
libpwdchan_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) -lpwdchan
libpwdchan_plugin_la_DEPENDENCIES = libslapd.la $(PWDCHAN_LIB)
libpwdchan_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpbe-plugin
#-----------------------
libpbe_plugin_la_SOURCES = ldap/servers/plugins/rever/pbe.c \
	ldap/servers/plugins/rever/rever.c

libpbe_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(SVRCORE_INCLUDES)
libpbe_plugin_la_LIBADD = libslapd.la libsvrcore.la $(NSS_LINK)
libpbe_plugin_la_DEPENDENCIES = libslapd.la
libpbe_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdistrib-plugin
#------------------------
libdistrib_plugin_la_SOURCES = ldap/servers/plugins/distrib/distrib.c

libdistrib_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libdistrib_plugin_la_LIBADD = libslapd.la
libdistrib_plugin_la_DEPENDENCIES = libslapd.la
libdistrib_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libdna-plugin
#------------------------
libdna_plugin_la_SOURCES = ldap/servers/plugins/dna/dna.c

libdna_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libdna_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libdna_plugin_la_DEPENDENCIES = libslapd.la
libdna_plugin_la_LDFLAGS = -avoid-version

#------------------------
# liblinkedattrs-plugin
#------------------------
liblinkedattrs_plugin_la_SOURCES = ldap/servers/plugins/linkedattrs/fixup_task.c \
	ldap/servers/plugins/linkedattrs/linked_attrs.c

liblinkedattrs_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
liblinkedattrs_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
liblinkedattrs_plugin_la_DEPENDENCIES = libslapd.la
liblinkedattrs_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libmanagedentries-plugin
#------------------------
libmanagedentries_plugin_la_SOURCES = ldap/servers/plugins/mep/mep.c

libmanagedentries_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libmanagedentries_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libmanagedentries_plugin_la_DEPENDENCIES = libslapd.la
libmanagedentries_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libmemberof-plugin
#------------------------
libmemberof_plugin_la_SOURCES= ldap/servers/plugins/memberof/memberof.c \
	ldap/servers/plugins/memberof/memberof_config.c

libmemberof_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libmemberof_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libmemberof_plugin_la_DEPENDENCIES = libslapd.la
libmemberof_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libpam-passthru-plugin
#------------------------
libpam_passthru_plugin_la_SOURCES = ldap/servers/plugins/pam_passthru/pam_ptconfig.c \
	ldap/servers/plugins/pam_passthru/pam_ptdebug.c \
	ldap/servers/plugins/pam_passthru/pam_ptimpl.c \
	ldap/servers/plugins/pam_passthru/pam_ptpreop.c

libpam_passthru_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libpam_passthru_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK) $(PAM_LINK)
libpam_passthru_plugin_la_DEPENDENCIES = libslapd.la
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

libpassthru_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libpassthru_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libpassthru_plugin_la_DEPENDENCIES = libslapd.la
libpassthru_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libposix-winsync-plugin
#------------------------
libposix_winsync_plugin_la_SOURCES = ldap/servers/plugins/posix-winsync/posix-winsync.c \
	ldap/servers/plugins/posix-winsync/posix-group-func.c \
	ldap/servers/plugins/posix-winsync/posix-group-task.c \
	ldap/servers/plugins/posix-winsync/posix-winsync-config.c

libposix_winsync_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) -DWINSYNC_TEST_POSIX \
	-I$(srcdir)/ldap/servers/plugins/replication
libposix_winsync_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libposix_winsync_plugin_la_DEPENDENCIES = libslapd.la
libposix_winsync_plugin_la_LDFLAGS = -avoid-version

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
	ldap/servers/plugins/pwdstorage/ssha_pwd.c \
	ldap/servers/plugins/pwdstorage/pbkdf2_pwd.c \
	ldap/servers/plugins/pwdstorage/gost_yescrypt.c \
	$(NULLSTRING)

libpwdstorage_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libpwdstorage_plugin_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK) $(LIBCRYPT)
libpwdstorage_plugin_la_DEPENDENCIES = libslapd.la
libpwdstorage_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libcontentsync-plugin
#------------------------
libcontentsync_plugin_la_SOURCES = ldap/servers/plugins/sync/sync_init.c \
	ldap/servers/plugins/sync/sync_util.c \
	ldap/servers/plugins/sync/sync_refresh.c \
	ldap/servers/plugins/sync/sync_persist.c

libcontentsync_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libcontentsync_plugin_la_LIBADD = libslapd.la $(NSS_LINK) $(NSPR_LINK) $(LIBCRYPT)
libcontentsync_plugin_la_DEPENDENCIES = libslapd.la
libcontentsync_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libreferint-plugin
#------------------------
libreferint_plugin_la_SOURCES = ldap/servers/plugins/referint/referint.c

libreferint_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libreferint_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libreferint_plugin_la_DEPENDENCIES = libslapd.la
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
	ldap/servers/plugins/replication/llist.c \
	ldap/servers/plugins/replication/repl_connext.c \
	ldap/servers/plugins/replication/repl_controls.c \
	ldap/servers/plugins/replication/repl_ext.c \
	ldap/servers/plugins/replication/repl_extop.c \
	ldap/servers/plugins/replication/repl_globals.c \
	ldap/servers/plugins/replication/repl_opext.c \
	ldap/servers/plugins/replication/repl_session_plugin.c \
	ldap/servers/plugins/replication/repl_cleanallruv.c \
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

libreplication_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(ICU_CFLAGS) $(DB_INC)
libreplication_plugin_la_LIBADD = libslapd.la libback-ldbm.la $(LDAPSDK_LINK) $(NSS_LINK) $(NSPR_LINK) $(ICU_LIBS) $(DB_LINK)
libreplication_plugin_la_DEPENDENCIES = libslapd.la libback-ldbm.la
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

libretrocl_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libretrocl_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libretrocl_plugin_la_DEPENDENCIES = libslapd.la
libretrocl_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libroles-plugin
#------------------------
libroles_plugin_la_SOURCES = ldap/servers/plugins/roles/roles_cache.c \
	ldap/servers/plugins/roles/roles_plugin.c

libroles_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libroles_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libroles_plugin_la_DEPENDENCIES = libslapd.la
libroles_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libschemareload-plugin
#------------------------
libschemareload_plugin_la_SOURCES = ldap/servers/plugins/schema_reload/schema_reload.c

libschemareload_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libschemareload_plugin_la_LIBADD = libslapd.la $(NSPR_LINK)
libschemareload_plugin_la_DEPENDENCIES = libslapd.la
libschemareload_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libstatechange-plugin
#------------------------
libstatechange_plugin_la_SOURCES = ldap/servers/plugins/statechange/statechange.c

libstatechange_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libstatechange_plugin_la_LIBADD = libslapd.la
libstatechange_plugin_la_DEPENDENCIES = libslapd.la
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

libsyntax_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libsyntax_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libsyntax_plugin_la_DEPENDENCIES = libslapd.la
libsyntax_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libusn-plugin
#------------------------
libusn_plugin_la_SOURCES = ldap/servers/plugins/usn/usn.c \
                           ldap/servers/plugins/usn/usn_cleanup.c

libusn_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libusn_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libusn_plugin_la_DEPENDENCIES = libslapd.la
libusn_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libviews-plugin
#------------------------
libviews_plugin_la_SOURCES = ldap/servers/plugins/views/views.c

libviews_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libviews_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libviews_plugin_la_DEPENDENCIES = libslapd.la
libviews_plugin_la_LDFLAGS = -avoid-version

#------------------------
# libwhoami-plugin
#------------------------
libwhoami_plugin_la_SOURCES = ldap/servers/plugins/whoami/whoami.c

libwhoami_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
libwhoami_plugin_la_LIBADD = libslapd.la $(LDAPSDK_LINK) $(NSPR_LINK)
libwhoami_plugin_la_DEPENDENCIES = libslapd.la
libwhoami_plugin_la_LDFLAGS = -avoid-version

#------------------------
#////////////////////////////////////////////////////////////////
#
#   Programs
#
#////////////////////////////////////////////////////////////////
#------------------------
# dbscan
#------------------------
dbscan_SOURCES = ldap/servers/slapd/tools/dbscan.c

dbscan_CPPFLAGS = $(NSPR_INCLUDES) $(AM_CPPFLAGS)
dbscan_LDADD = $(NSPR_LINK) $(DB_IMPL) libslapd.la

#------------------------
# ldap-agent
#------------------------
ldap_agent_SOURCES = ldap/servers/snmp/main.c \
	ldap/servers/snmp/ldap-agent.c \
	ldap/servers/slapd/agtmmap.c

ldap_agent_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) @netsnmp_inc@
ldap_agent_LDADD = $(LDAPSDK_LINK) $(SASL_LINK) $(NSS_LINK) $(NSPR_LINK) $(NETSNMP_LINK) $(THREADLIB)


#------------------------
# ldclt
#------------------------
ldclt_SOURCES = ldap/servers/slapd/tools/ldaptool-sasl.c \
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

ldclt_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ldap/servers/slapd/tools $(DSPLUGIN_CPPFLAGS) $(SASL_CFLAGS)
ldclt_LDADD = $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK) $(LIBNSL) $(LIBSOCKET) $(LIBDL) $(THREADLIB)

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
	ldap/servers/slapd/psearch.c \
	ldap/servers/slapd/pw_mgmt.c \
	ldap/servers/slapd/pw_verify.c \
	ldap/servers/slapd/rootdse.c \
	ldap/servers/slapd/sasl_io.c \
	ldap/servers/slapd/saslbind.c \
	ldap/servers/slapd/search.c \
	ldap/servers/slapd/start_tls_extop.c \
	ldap/servers/slapd/strdup.c \
	ldap/servers/slapd/stubs.c \
	ldap/servers/slapd/tempnam.c  \
	ldap/servers/slapd/unbind.c \
	ldap/servers/slapd/subentries.c

ns_slapd_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(SASL_CFLAGS) $(SVRCORE_INCLUDES) $(CFI_CFLAGS)
# We need our libraries to come first, then our externals libraries second.
ns_slapd_LDADD = libslapd.la libldaputil.la libsvrcore.la $(RNSSLAPD_LIB)

ns_slapd_LDADD += $(LDAPSDK_LINK) $(NSS_LINK) $(LIBADD_DL) $(OPENSSL_LIBS) \
       $(NSPR_LINK) $(SASL_LINK) $(LIBNSL) $(LIBSOCKET) $(THREADLIB) $(SYSTEMD_LIBS) $(EVENT_LINK)

ns_slapd_DEPENDENCIES = libslapd.la libldaputil.la
ns_slapd_LINK = $(LINK)


#------------------------
# pwdhash
#------------------------
pwdhash_SOURCES = ldap/servers/slapd/tools/pwenc.c

pwdhash_CPPFLAGS = $(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS)
pwdhash_LDADD = libslapd.la libsvrcore.la $(NSPR_LINK) $(NSS_LINK) $(LDAPSDK_LINK) $(SASL_LINK)
pwdhash_DEPENDENCIES = libslapd.la

#-------------------------
# CMOCKA TEST PROGRAMS
#-------------------------
if ENABLE_CMOCKA

check_PROGRAMS = test_slapd
# Mark all check programs for testing
TESTS = test_slapd

test_slapd_SOURCES = test/main.c \
	test/libslapd/test.c \
	test/libslapd/counters/atomic.c \
	test/libslapd/filter/optimise.c \
	test/libslapd/pblock/analytics.c \
	test/libslapd/pblock/v3_compat.c \
	test/libslapd/schema/filter_validate.c \
	test/libslapd/operation/v3_compat.c \
	test/libslapd/spal/meminfo.c \
	test/plugins/test.c \
	test/plugins/pwdstorage/pbkdf2.c

# We need to link a lot of plugins for this test.
test_slapd_LDADD =	libslapd.la \
					libpwdstorage-plugin.la \
					$(NSS_LINK) $(NSPR_LINK)
test_slapd_LDFLAGS = $(AM_CPPFLAGS) $(CMOCKA_LINKS)
### WARNING: Slap.h needs cert.h, which requires the -I/lib/ldaputil!!!
### WARNING: Slap.h pulls ssl.h, which requires nss!!!!
# We need to pull in plugin header paths too:
test_slapd_CPPFLAGS =	$(AM_CPPFLAGS) $(DSPLUGIN_CPPFLAGS) $(DSINTERNAL_CPPFLAGS) \
						-I$(srcdir)/ldap/servers/plugins/pwdstorage

endif
#------------------------
# end cmocka tests
#------------------------

# these are for the config files and scripts that we need to generate and replace
# the paths and other tokens with the real values set during configure/make
# note that we cannot just use AC_OUTPUT to do this for us, since it will do things like this:
# LD_LIBRARY_PATH = ${prefix}/lib/dirsrv
# i.e. it literally copies in '${prefix}' rather than expanding it out - we want this instead:
# LD_LIBRARY_PATH = /usr/lib/dirsrv
fixupcmd = sed \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@libexecdir\@,$(libexecdir),g' \
	-e 's,@nss_libdir\@,$(nss_libdir),g' \
	-e 's,@ldaptool_bindir\@,$(ldaptool_bindir),g' \
	-e 's,@ldaptool_opts\@,$(ldaptool_opts),g' \
	-e 's,@plainldif_opts\@,$(plainldif_opts),g' \
	-e 's,@db_libdir\@,$(db_libdir),g' \
	-e 's,@db_bindir\@,$(db_bindir),g' \
	-e 's,@netsnmp_libdir\@,$(netsnmp_libdir),g' \
	-e 's,@propertydir\@,$(propertydir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@schemadir\@,$(schemadir),g' \
	-e 's,@serverdir\@,$(serverdir),g' \
	-e 's,@serverincdir\@,$(serverincdir),g' \
	-e 's,@serverplugindir\@,$(serverplugindir),g' \
	-e 's,@taskdir\@,$(taskdir),g' \
	-e 's,@configdir\@,$(configdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@localrundir\@,$(localrundir),g' \
	-e 's,@infdir\@,$(infdir),g' \
	-e 's,@mibdir\@,$(mibdir),g' \
	-e 's,@cockpitdir\@,$(cockpitdir),g' \
	-e 's,@templatedir\@,$(sampledatadir),g' \
	-e 's,@systemschemadir\@,$(systemschemadir),g' \
	-e 's,@package_name\@,$(PACKAGE_NAME),g' \
	-e 's,@instconfigdir\@,$(instconfigdir),g' \
	-e 's,@enable_ldapi\@,$(enable_ldapi),g' \
	-e 's,@enable_pam_passthru\@,$(enable_pam_passthru),g' \
	-e 's,@enable_bitwise\@,$(enable_bitwise),g' \
	-e 's,@enable_dna\@,$(enable_dna),g' \
	-e 's,@enable_autobind\@,$(enable_autobind),g' \
	-e 's,@enable_auto_dn_suffix\@,$(enable_auto_dn_suffix),g' \
	-e 's,@enable_presence\@,$(enable_presence),g' \
	-e 's,@enable_asan\@,$(ASAN_ON),g' \
	-e 's,@enable_msan\@,$(MSAN_ON),g' \
	-e 's,@enable_tsan\@,$(TSAN_ON),g' \
	-e 's,@enable_ubsan\@,$(UBSAN_ON),g' \
	-e 's,@SANITIZER\@,$(SANITIZER),g' \
	-e 's,@ECHO_N\@,$(ECHO_N),g' \
	-e 's,@ECHO_C\@,$(ECHO_C),g' \
	-e 's,@brand\@,$(brand),g' \
	-e 's,@capbrand\@,$(capbrand),g' \
	-e 's,@vendor\@,$(vendor),g' \
	-e 's,@PACKAGE_NAME\@,$(PACKAGE_NAME),g' \
	-e 's,@PACKAGE_VERSION\@,$(PACKAGE_VERSION),g' \
	-e 's,@RPM_VERSION\@,$(RPM_VERSION),g' \
	-e 's,@PACKAGE_BASE_VERSION\@,$(PACKAGE_BASE_VERSION),g' \
	-e 's,@CONSOLE_VERSION\@,$(CONSOLE_VERSION),g' \
	-e 's,@BUILDNUM\@,$(BUILDNUM),g' \
	-e 's,@NQBUILD_NUM\@,$(NQBUILDNUM),g' \
	-e 's,@perlpath\@,$(perldir),g' \
	-e 's,@defaultuser\@,$(defaultuser),g' \
	-e 's,@defaultgroup\@,$(defaultgroup),g' \
	-e 's,@with_fhs_opt\@,@with_fhs_opt@,g' \
	-e 's,@with_selinux\@,@with_selinux@,g' \
	-e 's,@with_systemd\@,$(WITH_SYSTEMD),g' \
	-e 's,@tmpfiles_d\@,$(tmpfiles_d),g' \
	-e 's,@pythonexec\@,@pythonexec@,g' \
	-e 's,@sttyexec\@,@sttyexec@,g' \
	-e 's,@initconfigdir\@,$(initconfigdir),g' \
	-e 's,@updatedir\@,$(updatedir),g' \
	-e 's,@ldaplib\@,$(ldaplib),g' \
	-e 's,@ldaplib_defs\@,$(ldaplib_defs),g' \
	-e 's,@systemdsystemunitdir\@,$(systemdsystemunitdir),g' \
	-e 's,@systemdsystemconfdir\@,$(systemdsystemconfdir),g' \
	-e 's,@systemdgroupname\@,$(systemdgroupname),g' \
	-e 's,@prefixdir\@,$(prefixdir),g'

%: %.in
	mkdir -p $(dir $@)
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME): %/initscript.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME): %/base-initconfig.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
if SYSTEMD
	$(fixupcmd) $^ | sed -e 's/@preamble@/# This file is in systemd EnvironmentFile format - see man systemd.exec/' > $@
else
	$(fixupcmd) $^ | sed -n -e 's/@preamble@//' -e '/^#/{p;d;}' -e '/^$$/{p;d;}' -e 's/^\([^=]*\)\(=.*\)$$/\1\2 ; export \1/ ; p' > $@
	$(fixupcmd) $(srcdir)/ldap/admin/src/initconfig.in >> $@
endif

%/template-initconfig: %/template-initconfig.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
if SYSTEMD
	$(fixupcmd) $^ | sed -e 's/@preamble@/# This file is in systemd EnvironmentFile format - see man systemd.exec/' > $@
else
	$(fixupcmd) $^ | sed -n -e 's/@preamble@//' -e '/^#/{p;d;}' -e '/^$$/{p;d;}' -e 's/^\([^=]*\)\(=.*\)$$/\1\2 ; export \1/ ; p' > $@
endif

%/$(PACKAGE_NAME)-snmp: %/ldap-agent-initscript.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

# yes, that is an @ in the filename . . .
%/$(PACKAGE_NAME)@.service: %/systemd.template.service.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME)@.service.d/custom.conf: %/systemd.template.service.custom.conf.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

if with_sanitizer
%/$(PACKAGE_NAME)@.service.d/xsan.conf: %/systemd.template.service.xsan.conf.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@
endif

%/$(systemdgroupname): %/systemd.group.in
	if [ ! -d $(dir $@) ] ; then mkdir -p $(dir $@) ; fi
	$(fixupcmd) $^ > $@

%/$(PACKAGE_NAME)-snmp.service: %/systemd-snmp.service.in
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

# Python test tests
# How will we update this to python 3?

lib389: src/lib389/setup.py
	cd $(srcdir)/src/lib389; $(PYTHON) setup.py build ; $(PYTHON) setup.py build_manpages

lib389-install: lib389
	cd $(srcdir)/src/lib389; $(PYTHON) setup.py install --skip-build --force

if ENABLE_COCKPIT

NODE_MODULES_TEST = src/cockpit/389-console/node_modules/webpack
WEBPACK_TEST = src/cockpit/389-console/cockpit_dist/index.html

# Cockpit UI plugin - we install the dependancies and build the JS sources
# and then we use install-data-hook for copying the results on 'make install'
$(NODE_MODULES_TEST):
	cd src/cockpit/389-console; make -f node_modules.mk install

$(WEBPACK_TEST): $(NODE_MODULES_TEST)
	cd src/cockpit/389-console; make -f node_modules.mk build-cockpit-plugin

389-console: $(WEBPACK_TEST)

# This requires a built source tree and avoids having to install anything system-wide
389-console-devel-install:
	cd $(srcdir)/src/cockpit/389-console; \
	rm ~/.local/share/cockpit/389-console; \
	mkdir -p ~/.local/share/cockpit/; \
	ln -s $$(pwd)/dist ~/.local/share/cockpit/389-console

389-console-clean:
	cd $(srcdir)/src/cockpit/389-console; make -f node_modules.mk clean

endif

if HAVE_DOXYGEN

# The rm in man3 is to remove files like: _home_william_development_389ds_libsds_src_.3
# If there is a way to ignore this in doxygen I'm all ears ...

doxyfile.stamp:
	cd $(srcdir); $(DOXYGEN) $(abs_top_builddir)/docs/slapi.doxy
	rm -f $(abs_top_builddir)/man/man3/_*
	touch doxyfile.stamp

# Add the docs to make all.
all-local: doxyfile.stamp

endif
