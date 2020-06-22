#!/bin/sh
#
# Do a fresh build of the UI, and run it.  While in this state all updates made
# are built immediately.  Just refresh the browser to test them.

AUDIT=0
while (( "$#" )); do
    case "$1" in
        -a|--audit)
        AUDIT=1
        break
        ;;
    -h|--help)
        echo Usage:
        echo This is a development script to quickly refresh the UI and watch it live
        echo Options:
        echo    -a|--audit    Audit the build
        exit 0
        ;;
    -*|--*=)
        echo "Error: Unsupported argument $1" >&2
        echo "Available Options:" >&2
        echo "   -a|--audit    Audit the build" >&2
        exit 1
        ;;
    esac
done

printf "\nCleaning and installing npm packages ...\n\n"
rm -rf dist/*
make -f node_modules.mk clean > /dev/null
make -f node_modules.mk install > /dev/null
if [ $? != 0 ]; then
    exit 1
fi

if [ $AUDIT == 1 ]; then
    printf "\nAuditing npm packages ...\n\n"
    make -f node_modules.mk build-cockpit-plugin
    if [ $? != 0 ]; then
        exit 1
    fi
fi

printf "\nBuilding and watching ...\n"
node_modules/webpack/bin/webpack.js --watch
