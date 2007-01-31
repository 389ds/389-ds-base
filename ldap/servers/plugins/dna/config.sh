#!/bin/sh
ldapadd -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -f dna.ldif -c
ldapadd -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -f posix.ldif -c
ldapadd -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -f subtest.ldif -c

