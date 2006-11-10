/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


void	configure __P((char *));
DB_ENV *db_init __P((char *));
void	pheader __P((DB *, int));
void	usage __P((void));

const char
	*progname = "db_dump";				/* Program name. */

int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind;
	DB *dbp;
	DBC *dbcp;
	DBT key, data;
	DB_ENV *dbenv;
	int ch, checkprint, dflag;
	char *home;

	home = NULL;
	checkprint = dflag = 0;
	while ((ch = getopt(argc, argv, "df:h:p")) != EOF)
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'f':
			if (freopen(optarg, "w", stdout) == NULL)
				err(1, "%s", optarg);
			break;
		case 'h':
			home = optarg;
			break;
		case 'p':
			checkprint = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (dflag) {
		if (home != NULL)
			errx(1,
			    "the -d and -h options may not both be specified");
		if (checkprint)
			errx(1,
			    "the -d and -p options may not both be specified");
	}
	/* Initialize the environment. */
	dbenv = dflag ? NULL : db_init(home);

	/* Open the DB file. */
	if ((errno =
	    db_open(argv[0], DB_UNKNOWN, DB_RDONLY, 0, dbenv, NULL, &dbp)) != 0)
		err(1, "%s", argv[0]);

	/* DB dump. */
	if (dflag) {
		(void)__db_dump(dbp, NULL, 1);
		if ((errno = dbp->close(dbp, 0)) != 0)
			err(1, "close");
		exit (0);
	}

	/* Get a cursor and step through the database. */
	if ((errno = dbp->cursor(dbp, NULL, &dbcp)) != 0) {
		(void)dbp->close(dbp, 0);
		err(1, "cursor");
	}

	/* Print out the header. */
	pheader(dbp, checkprint);

	/* Print out the key/data pairs. */
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	while ((errno = dbcp->c_get(dbcp, &key, &data, DB_NEXT)) == 0) {
		if (dbp->type != DB_RECNO &&
		    (errno = __db_prdbt(&key, checkprint, stdout)) != 0)
			break;
		if ((errno = __db_prdbt(&data, checkprint, stdout)) != 0)
			break;
	}

	if (errno != DB_NOTFOUND)
		err(1, "cursor get");

	if ((errno = dbp->close(dbp, 0)) != 0)
		err(1, "close");
	return (0);
}

/*
 * db_init --
 *	Initialize the environment.
 */
DB_ENV *
db_init(home)
	char *home;
{
	DB_ENV *dbenv;

	if ((dbenv = (DB_ENV *)calloc(sizeof(DB_ENV), 1)) == NULL) {
		errno = ENOMEM;
		err(1, NULL);
	}
	dbenv->db_errfile = stderr;
	dbenv->db_errpfx = progname;

	if ((errno =
	    db_appinit(home, NULL, dbenv, DB_CREATE | DB_USE_ENVIRON)) != 0)
		err(1, "db_appinit");
	return (dbenv);
}

/*
 * pheader --
 *	Write out the header information.
 */
void
pheader(dbp, pflag)
	DB *dbp;
	int pflag;
{
	DB_BTREE_STAT *btsp;
	HTAB *hashp;
	HASHHDR *hdr;
	db_pgno_t pgno;

	printf("format=%s\n", pflag ? "print" : "bytevalue");
	switch (dbp->type) {
	case DB_BTREE:
		printf("type=btree\n");
		if ((errno = dbp->stat(dbp, &btsp, NULL, 0)) != 0)
			err(1, "dbp->stat");
		if (F_ISSET(dbp, DB_BT_RECNUM))
			printf("recnum=1\n");
		if (btsp->bt_maxkey != 0)
			printf("bt_maxkey=%lu\n", (u_long)btsp->bt_maxkey);
		if (btsp->bt_minkey != 0)
			printf("bt_minkey=%lu\n", (u_long)btsp->bt_minkey);
		break;
	case DB_HASH:
		printf("type=hash\n");
		hashp = dbp->internal;
		pgno = PGNO_METADATA;
		if (memp_fget(dbp->mpf, &pgno, 0, &hdr) == 0) {
			if (hdr->ffactor != 0)
				printf("h_ffactor=%lu\n", (u_long)hdr->ffactor);
			if (hdr->nelem != 0)
				printf("h_nelem=%lu\n", (u_long)hdr->nelem);
			(void)memp_fput(dbp->mpf, hdr, 0);
		}
		break;
	case DB_RECNO:
		printf("type=recno\n");
		if (F_ISSET(dbp, DB_RE_RENUMBER))
			printf("renumber=1\n");
		if (F_ISSET(dbp, DB_RE_FIXEDLEN))
			printf("re_len=%lu\n", (u_long)btsp->bt_re_len);
		if (F_ISSET(dbp, DB_RE_PAD))
			printf("re_pad=%#x\n", btsp->bt_re_pad);
		break;
	case DB_UNKNOWN:
		abort();
		/* NOTREACHED */
	}

	if (F_ISSET(dbp, DB_AM_DUP))
		printf("duplicates=1\n");

	if (dbp->dbenv->db_lorder != 0)
		printf("db_lorder=%lu\n", (u_long)dbp->dbenv->db_lorder);

	if (!F_ISSET(dbp, DB_AM_PGDEF))
		printf("db_pagesize=%lu\n", (u_long)dbp->pgsize);

	printf("HEADER=END\n");
}

/*
 * usage --
 *	Display the usage message.
 */
void
usage()
{
	(void)fprintf(stderr,
	    "usage: db_dump [-dp] [-f file] [-h home] db_file\n");
	exit(1);
}
