@echo off
cd passsync
nmake passsync.mak
copy Debug\passsync.exe ..\Wix
cd ..\passhook
nmake passhook.mak
copy Debug\passhook.dll ..\Wix

if EXIST ..\Wix (goto :WIX)

goto :EOF
:WIX

cd ..\Wix
if NOT EXIST candle.exe ( 
	echo ERROR: Wix not properly installed.  See readme.
	pause
	exit 1 )

if NOT EXIST light.exe ( 
	echo ERROR: Wix not properly installed.  See readme.
	pause
	exit 1 )

candle PassSync.wxs
light PassSync.wixobj

