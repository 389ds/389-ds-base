@rem //
@rem // BEGIN COPYRIGHT BLOCK
@rem // Copyright 2001 Sun Microsystems, Inc.
@rem // Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

