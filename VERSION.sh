# brand is lower case - used for names that don't appear to end users
# brand is used for file naming - should contain no spaces
brand=389
# capbrand is the properly capitalized brand name that appears to end users
# may contain spaces
capbrand=389
# vendor is the properly formatted vendor/manufacturer name that appears to end users
vendor="389 Project"

# PACKAGE_VERSION is constructed from these
VERSION_MAJOR=2
VERSION_MINOR=3
VERSION_MAINT=4
# NOTE: VERSION_PREREL is automatically set for builds made out of a git tree
VERSION_PREREL=
VERSION_DATE=$(date -u +%Y%m%d%H%M)

# Set the version and release numbers for local developer RPM builds. We
# set these here because we do not want the git commit hash in the RPM
# version since it can make RPM upgrades difficult.  If we have a git
# commit hash, we add it into the release number below.
RPM_RELEASE=${VERSION_DATE}
RPM_VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_MAINT}

if $(git -C "$srcdir" rev-parse --is-inside-work-tree > /dev/null 2>&1); then
# Check if the source is from a git repo
# if this is not a git repo, git log will say
#  fatal: Not a git repository
# to stderr and stdout will be empty
# this tells git to print the short commit hash from the last commit
    COMMIT=$(git -C "$srcdir" log -1 --pretty=format:%h 2> /dev/null)
    if test -n "$COMMIT" ; then
        VERSION_PREREL=.${VERSION_DATE}git$COMMIT
        RPM_RELEASE=${RPM_RELEASE}git$COMMIT
    fi
fi

# the real version used throughout configure and make
# NOTE: because of autoconf/automake harshness, we cannot override the settings
# below in C code - there is no way to override the default #defines
# for these set with AC_INIT - so configure.ac should AC_DEFINE
# DS_PACKAGE_VERSION DS_PACKAGE_TARNAME DS_PACKAGE_BUGREPORT
# for use in C code - other code (perl scripts, shell scripts, Makefiles)
# can use PACKAGE_VERSION et. al.
PACKAGE_VERSION=$VERSION_MAJOR.$VERSION_MINOR.${VERSION_MAINT}${VERSION_PREREL}
# the name of the source tarball - see make dist
PACKAGE_TARNAME=${brand}-ds-base
# url for bug reports
PACKAGE_BUGREPORT="${PACKAGE_BUGREPORT}enter_bug.cgi?product=$brand"
# PACKAGE_STRING="$PACKAGE_TARNAME $PACKAGE_VERSION"
# the version of the ds console package that this directory server
# is compatible with
# console .2 is still compatible with 389 .3 for now
CONSOLE_VERSION=$VERSION_MAJOR.2
