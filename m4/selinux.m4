# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2009 Red Hat, Inc.
# All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# END COPYRIGHT BLOCK

AC_CHECKING(for SELinux)

# check for --with-selinux
AC_MSG_CHECKING(for --with-selinux)
AC_ARG_WITH(selinux, [  --with-selinux   Build SELinux policy],
[
  with_selinux=yes
  AC_MSG_RESULT(yes)
  AC_SUBST(with_selinux)
  if test ! -f "/usr/share/selinux/devel/Makefile"; then
    AC_MSG_ERROR([SELinux development tools (selinux-policy) not found])
  fi
],
AC_MSG_RESULT(no))
