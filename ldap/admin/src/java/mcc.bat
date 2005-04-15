@rem //
@rem // BEGIN COPYRIGHT BLOCK
@rem // This Program is free software; you can redistribute it and/or modify it under
@rem // the terms of the GNU General Public License as published by the Free Software
@rem // Foundation; version 2 of the License.
@rem // 
@rem // This Program is distributed in the hope that it will be useful, but WITHOUT
@rem // ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
@rem // FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
@rem // 
@rem // You should have received a copy of the GNU General Public License along with
@rem // this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
@rem // Place, Suite 330, Boston, MA 02111-1307 USA.
@rem // 
@rem // In addition, as a special exception, Red Hat, Inc. gives You the additional
@rem // right to link the code of this Program with code not covered under the GNU
@rem // General Public License ("Non-GPL Code") and to distribute linked combinations
@rem // including the two, subject to the limitations in this paragraph. Non-GPL Code
@rem // permitted under this exception must only link to the code of this Program
@rem // through those well defined interfaces identified in the file named EXCEPTION
@rem // found in the source code files (the "Approved Interfaces"). The files of
@rem // Non-GPL Code may instantiate templates or use macros or inline functions from
@rem // the Approved Interfaces without causing the resulting work to be covered by
@rem // the GNU General Public License. Only Red Hat, Inc. may make changes or
@rem // additions to the list of Approved Interfaces. You must obey the GNU General
@rem // Public License in all respects for all of the Program code and other code used
@rem // in conjunction with the Program except the Non-GPL Code covered by this
@rem // exception. If you modify this file, you may extend this exception to your
@rem // version of the file, but you are not obligated to do so. If you do not wish to
@rem // do so, delete this exception statement from your version. 
@rem // 
@rem // 
@rem // Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
@rem // Copyright (C) 2005 Red Hat, Inc.
@rem // All rights reserved.
@rem // END COPYRIGHT BLOCK
@rem //
@echo off
setlocal
set MCC_HOST=%1
set MCC_PORT=%2
if "%MCC_HOST%x"=="x" goto usage
if "%MCC_PORT%x"=="x" goto usage
rem
set MCC_USER=
if %3x==x goto nouser
set MCC_USER=-u %3
echo MCC_USER = %MCC_USER%
:nouser
set MCC_PASSWORD=
if %4x==x goto nopass
set MCC_PASSWORD=-w %4
:nopass
rem
set JARDIR=./jars
set JDK=%JARDIR%/classes.zip
set DS=%JARDIR%/ds40.jar
set ADMIN=%JARDIR%/admserv.jar
set KINGPIN=%JARDIR%/kingpin.jar
set SWING=%JARDIR%/swingall.jar
set LF=%JARDIR%/nmclf.jar
set LAYOUT=%JARDIR%/layout.jar
set LDAP=%JARDIR%/ldapjdk.jar
set BASE=o=netscaperoot
set CLASSPATH=%DS%;%KINGPIN%;%ADMIN%;%SWING%;%LAYOUT%;%LF%;%LDAP%
rem echo java com.netscape.management.client.console.Console -d %MCC_HOST% -p %MCC_PORT% %MCC_USER% %MCC_PASSWORD% -b %BASE%
java com.netscape.management.client.console.Console -d %MCC_HOST% -p %MCC_PORT% %MCC_USER% %MCC_PASSWORD% -b %BASE%
goto end

:usage
echo Usage: mcc HOST PORT [[user] [password]]

:end
endlocal

