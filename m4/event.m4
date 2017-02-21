# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2015  Red Hat
# see files 'COPYING' and 'COPYING.openssl' for use and warranty
# information
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 
# Additional permission under GPLv3 section 7:
# 
# If you modify this Program, or any covered work, by linking or
# combining it with OpenSSL, or a modified version of OpenSSL licensed
# under the OpenSSL license
# (https://www.openssl.org/source/license.html), the licensors of this
# Program grant you additional permission to convey the resulting
# work. Corresponding Source for a non-source form of such a
# combination shall include the source code for the parts that are
# licensed under the OpenSSL license as well as that of the covered
# work.
# END COPYRIGHT BLOCK
AC_CHECKING(for EVENT)

# Always use pkgconfig, because we know it's installed properly!

AC_MSG_CHECKING(for event with pkg-config)
AC_PATH_PROG(PKG_CONFIG, pkg-config)
if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists libevent; then
        event_inc=`$PKG_CONFIG --cflags libevent`
        event_lib=`$PKG_CONFIG --libs libevent`
        AC_MSG_RESULT([using system EVENT])
    else
        AC_MSG_ERROR([EVENT not found, check with pkg-config libevent!])
    fi
fi

AC_SUBST(event_inc)
AC_SUBST(event_lib)



