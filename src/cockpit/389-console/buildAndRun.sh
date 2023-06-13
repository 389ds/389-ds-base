#!/bin/sh
#
# Do a fresh build of the UI, and run it.  While in this state all updates made
# are built immediately.  Just refresh the browser to test them.

printf "\nCleaning and installing npm packages ...\n\n"
rm -rf dist/*
npm clean > /dev/null
npm install > /dev/null
if [ $? != 0 ]; then
    exit 1
fi

printf "\nBuilding and watching ...\n"
ESBUILD_WATCH=true ./build.js
# npm run watch
