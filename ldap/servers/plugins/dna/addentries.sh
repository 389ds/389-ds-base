#!/bin/sh
ldapadd -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -f posix_test.ldif -c
