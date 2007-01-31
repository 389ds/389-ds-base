#!/bin/sh
ldapdelete -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -f del_test_entries.dns -c
