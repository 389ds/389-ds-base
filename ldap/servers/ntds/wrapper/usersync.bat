@rem // --- BEGIN COPYRIGHT BLOCK ---
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
@rem // provide this exception without modification, you must delete this exception
@rem // statement from your version and license this file solely under the GPL without
@rem // exception. 
@rem // 
@rem // 
@rem // Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
@rem // Copyright (C) 2005 Red Hat, Inc.
@rem // All rights reserved.
@rem // --- END COPYRIGHT BLOCK ---
@echo off
setlocal

rem
rem Java Service Wrapper general startup script
rem

rem
rem Resolve the real path of the Wrapper.exe
rem  For non NT systems, the _REALPATH and _WRAPPER_CONF values
rem  can be hard-coded below and the following test removed.
rem
if "%OS%"=="Windows_NT" goto nt
echo This script only works with NT-based versions of Windows.
goto :eof

:nt
rem
rem Find the application home.
rem
rem %~dp0 is location of current script under NT
set _REALPATH=%~dp0
set _WRAPPER_EXE=%_REALPATH%Wrapper.exe

rem
rem Find the wrapper.conf
rem
:conf
set _WRAPPER_CONF="%~f1"
if not %_WRAPPER_CONF%=="" goto startup
set _WRAPPER_CONF="%_REALPATH%..\conf\wrapper.conf"

rem
rem Start the Wrapper
rem
:startup
"%_WRAPPER_EXE%" -c %_WRAPPER_CONF%
if not errorlevel 1 goto :eof
pause

