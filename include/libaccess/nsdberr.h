/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nsdberr_h
#define __nsdberr_h

/* NSDB facility name (defined in nsdb,c) */
extern char * NSDB_Program;

/* Define error identifiers for NSDB facility */

/* Errors generated in nsdb.c */

/* ndbFindName() */
#define NSDBERR1000	1000	/* primary DB get operation failed */

/* ndbIdToName() */
#define NSDBERR1100	1100	/* id-to-name DB get operation failed */

/* ndbInitPrimary() */
#define NSDBERR1200	1200	/* primary database already exists */
#define NSDBERR1220	1220	/* primary database open failed */
#define NSDBERR1240	1240	/* primary DB put operation failed */
#define NSDBERR1260	1260	/* primary DB put operation failed */

/* ndbOpen() */
#define NSDBERR1400	1400	/* insufficient dynamic memory */
#define NSDBERR1420	1420	/* insufficient dynamic memory */
#define NSDBERR1440	1440	/* insufficient dynamic memory */
#define NSDBERR1460	1460	/* primary DB get metadata operation failed */
#define NSDBERR1480	1480	/* metadata format error */
#define NSDBERR1500	1500	/* unsupported database version number */
#define NSDBERR1520	1520	/* wrong database type */

/* ndbReOpen() */
#define NSDBERR1600	1600	/* create primary DB failed */
#define NSDBERR1620	1620	/* open primary/write failed */
#define NSDBERR1640	1640	/* open primary/read failed */
#define NSDBERR1660	1660	/* create id-to-name DB failed */
#define NSDBERR1680	1680	/* open id-to-name DB for write failed */
#define NSDBERR1700	1700	/* open id-to-name DB for read failed */

/* Define error ids generated in nsdbmgmt.c */

/* ndbAllocId() */
#define NSDBERR2000	2000	/* bad DB name key */
#define NSDBERR2020	2020	/* metadata get operation failed */
#define NSDBERR2040	2040	/* no space to grow DB id bitmap */
#define NSDBERR2060	2060	/* no space to copy DB id bitmap */
#define NSDBERR2080	2080	/* put bitmap to DB operation failed */
#define NSDBERR2100	2100	/* put id-to-name operation failed */

/* ndbDeleteName() */
#define NSDBERR2200	2200	/* error deleting record */

/* ndbFreeId() */
#define NSDBERR2300	2300	/* invalid id value */
#define NSDBERR2320	2320	/* error deleting id-to-name record */
#define NSDBERR2340	2340	/* error reading id bitmap from primary DB */
#define NSDBERR2360	2360	/* invalid id value */
#define NSDBERR2380	2380	/* insufficient dynamic memory */
#define NSDBERR2400	2400	/* error writing id bitmap back to DB */

/* ndbRenameId() */
#define NSDBERR2500	2500	/* invalid new key name string */
#define NSDBERR2520	2520	/* get id record operation failed */
#define NSDBERR2540	2540	/* put id record operation failed */

/* ndbStoreName() */
#define NSDBERR2700	2700	/* database put operation failed */

/* Define error return codes */
#define NDBERRNOMEM	-1		/* insufficient dynamic memory */
#define NDBERRNAME	-2		/* invalid key name string */
#define NDBERROPEN	-3		/* database open error */
#define NDBERRMDGET	-4		/* database metadata get failed */
#define NDBERRMDPUT	-5		/* database metadata put failed */
#define NDBERRIDPUT	-6		/* id-to-name record put failed */
#define NDBERRNMDEL	-7		/* delete named record failed */
#define NDBERRPINIT	-8		/* error creating primary DB file */
#define NDBERRGET	-9		/* database get failed */
#define NDBERREXIST	-10		/* DB already exists */
#define NDBERRMDFMT	-11		/* invalid metadata format */
#define NDBERRDBTYPE	-12		/* wrong DB type */
#define NDBERRBADID	-13		/* invalid id value for name */
#define NDBERRPUT	-14		/* database put operation failed */
#define NDBERRVERS	-15		/* unsupported database version */
#define NDBERRIDDEL	-16		/* delete id-to-name record failed */

#endif /* __nsdberr_h */
