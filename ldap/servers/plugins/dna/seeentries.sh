#!/bin/sh
ldapsearch -x -D "cn=Directory Manager" -w secret123 -h localhost -p 3389 -b "dc=example, dc=com" "(objectclass=posixaccount)"
