/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dbtest.c - ldbm database test program */

#include "back-ldbm.h"

#define SLAPI_LDBM_DBTEST_OPT_DUMPDATA			0x0001
#define SLAPI_LDBM_DBTEST_OPT_KEY_IS_BINARY		0x0002
#define SLAPI_LDBM_DBTEST_OPT_DATA_IS_BINARY	0x0004
#define SLAPI_LDBM_DBTEST_OPT_DATA_IS_IDLIST	0x0008
#define SLAPI_LDBM_DBTEST_OPT_KEY_IS_ID			0x0010

static void		dbtest_help( void );
static void		dbtest_traverse( DB *db, char *filename, unsigned int options,
						FILE *outfp );
static void		dbtest_print_idlist( char *keystr, void *p,  u_int32_t size,
						FILE *outfp );
static void		dbtest_bprint( char *data, int len, char *lineprefix,
						FILE *outfp );

int ldbm_back_db_test( Slapi_PBlock *pb )
{
	char			buf[256], *instance_name;
	backend			*be;
	struct ldbminfo	*li;
    ldbm_instance	*inst;
	struct attrinfo	*ai;
	DB				*db;
	int				err, traversal_options;

	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );

	/* essential initialization */ 
	mapping_tree_init();
	ldbm_config_load_dse_info(li);
	/* Turn off transactions */
	ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");

    /* Find the instance */
    slapi_pblock_get( pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_name );
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (NULL == inst) {
		LDAPDebug(LDAP_DEBUG_ANY, "dbtest: unknown ldbm instance %s\n",
				instance_name, 0, 0);
		return -1;
    }

	/* store the be in the pb */
	be = inst->inst_be;
	slapi_pblock_set(pb, SLAPI_BACKEND, be);
	
    /***** prepare & init libdb, dblayer, and dbinstance *****/
	if (0 != dblayer_start(li, DBLAYER_TEST_MODE)) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "dbtest: Failed to init database\n", 0, 0, 0 );
		return( -1 );
	}
    if ( 0 != dblayer_instance_start(inst->inst_be, DBLAYER_NORMAL_MODE)) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "dbtest: failed to start instance\n", 0, 0, 0 );
		return( -1 );
	}

	/* display commands help test */
	dbtest_help();

	while ( 1 ) {
		traversal_options = 0;
		fputs( "dbtest: ", stdout );

		if ( fgets( buf, sizeof(buf), stdin ) == NULL )
			break;

		switch ( buf[0] ) {
		case 'i':
			traversal_options |= SLAPI_LDBM_DBTEST_OPT_DATA_IS_IDLIST;
			/*FALLTHRU*/

		case 't':
			traversal_options |= SLAPI_LDBM_DBTEST_OPT_DUMPDATA;
			/*FALLTHRU*/

		case 'T':
			/* read the index to traverse */
			fputs( " attr: ", stdout );
			if ( fgets( buf, sizeof(buf), stdin ) == NULL ) {
				exit( 0 );
			}
			buf[strlen( buf ) - 1] = '\0';
			ai = NULL;
			ainfo_get( be, buf, &ai );
			if ( ai == NULL ) {
				fprintf( stderr, "no index for %s\n", buf );
				continue;
			}

			/* open the index file */
			if ( (err = dblayer_get_index_file( be, ai, &db, 0 /* no create */ ))
			    != 0 ) {
				fprintf( stderr, "could not get index for %s (error %d - %s)\n",
				    buf, err, slapd_system_strerror( err ));
				continue;
			}

			/* traverse the file */
			traversal_options |= SLAPI_LDBM_DBTEST_OPT_DATA_IS_BINARY;
			dbtest_traverse( db, buf, traversal_options, stdout );

			/* clean up */
			dblayer_release_index_file( be, ai, db );
			break;

		case 'u':
			traversal_options |= SLAPI_LDBM_DBTEST_OPT_DUMPDATA;
			/*FALLTHRU*/

		case 'U':
			/* open the id2entry file */
			if ( (err = dblayer_get_id2entry( be, &db )) != 0 ) {
				fprintf( stderr, "could not get i2entry\n" );
				continue;
			}

			/* traverse the file */
			traversal_options |= SLAPI_LDBM_DBTEST_OPT_KEY_IS_ID;
			dbtest_traverse( db, "id2entry", traversal_options, stdout );

			/* clean up */
			dblayer_release_id2entry( be, db );
			break;

		default:
			dbtest_help();
			break;
		}
	}

	return( 0 );
}


static void
dbtest_help()
{
			puts( LDBM_DATABASE_TYPE_NAME " test mode" );
			puts( "\nindex key prefixes:" );
			printf( "          %c  presence      (sn=*)\n", PRES_PREFIX );
			printf( "          %c  equality      (sn=jensen)\n", EQ_PREFIX );
			printf( "          %c  approximate   (sn~=jensin)\n", APPROX_PREFIX );
			printf( "          %c  substring     (sn=jen*)\n", SUB_PREFIX );
			printf( "          %c  matching rule (sn:1.2.3.4.5:=Jensen)\n", RULE_PREFIX );
			printf( "          %c  continuation\n", CONT_PREFIX );

			puts( "\ncommands: i => traverse index keys and ID list values" );
			puts( "          t => traverse index keys and values" );
			puts( "          T => traverse index keys" );
			puts( "          u => traverse id2entry keys and values" );
			puts( "          U => traverse id2entry keys" );
#if 0
			puts( "          l<c> => lookup index" );
			puts( "          L<c> => lookup index (all)" );
			puts( "          t<c> => traverse index keys and values" );
			puts( "          T<c> => traverse index keys" );
			puts( "          x<c> => delete from index" );
			puts( "          e<c> => edit index entry" );
			puts( "          a<c> => add index entry" );
			puts( "          c<c> => create index" );
			puts( "          i<c> => insert ids into index" );
			puts( "          b    => change default backend" );
			puts( "          B    => print default backend" );
			puts( "          d<n> => set slapd_ldap_debug to n" );
			puts( "where <c> is a char selecting the index:" );
			puts( "          c => id2children" );
			puts( "          d => dn2id" );
			puts( "          e => id2entry" );
			puts( "          f => arbitrary file" );
			puts( "          i => attribute index" );
#endif /* 0 */
}


/*
 * get a cursor and walk over the databasea
 */
static void
dbtest_traverse( DB *db, char *filename, unsigned int options, FILE *outfp )
{
	DBC				*dbc;
	DBT				key, data;

	dbc = NULL;
	if ( db->cursor( db, NULL, &dbc, 0 ) != 0 ) {
		fprintf( stderr, "could not get cursor for %s\n", filename );
		return;
	}

	memset( &key, 0, sizeof(key) );
	memset( &data, 0, sizeof(data) );
	key.flags = DB_DBT_MALLOC;
	data.flags = DB_DBT_MALLOC;
	while ( dbc->c_get( dbc, &key, &data, DB_NEXT ) == 0 ) {
		if (( options & SLAPI_LDBM_DBTEST_OPT_KEY_IS_BINARY ) != 0 ) {
			fputs( "\tkey: ", outfp );
			dbtest_bprint( key.data, key.size, "\t      ", outfp );
		} else if (( options & SLAPI_LDBM_DBTEST_OPT_KEY_IS_ID ) != 0 ) {
			fprintf( outfp, "\tkey: %ld\n",
					(u_long)id_stored_to_internal( (char *)key.data ));
		} else {
			fprintf( outfp, "\tkey: %s\n", (char *)key.data );
		}
		if (( options & SLAPI_LDBM_DBTEST_OPT_DUMPDATA ) != 0 ) {
			if (( options & SLAPI_LDBM_DBTEST_OPT_DATA_IS_IDLIST ) != 0 ) {
				fputs( "\tdata: ", outfp );
				dbtest_print_idlist( (char *)key.dptr, data.data, data.size,
						outfp );
			} else if (( options & SLAPI_LDBM_DBTEST_OPT_DATA_IS_BINARY ) != 0 ) {
				fputs( "\tdata: ", outfp );
				dbtest_bprint( data.data, data.size, "\t      ", outfp );
			} else {
				fprintf( outfp, "\tdata: %s\n", (char *)data.data );
			}
		}
		free( key.data );
		free( data.data );
	}
	dbc->c_close(dbc);
}

static void
dbtest_print_idlist( char *keystr, void *p,  u_int32_t size, FILE *outfp )
{
	IDList	*idl;
	ID		i;

	idl = (IDList *)p;
	if ( ALLIDS( idl )) {
		fputs( "ALLIDS block\n", outfp );
	} else if ( INDIRECT_BLOCK( idl )) {
		fputs( "Indirect block)\n", outfp );
		for ( i = 0; idl->b_ids[i] != NOID; ++i ) {
			fprintf( outfp, "\t\tkey: %c%s%lu\n", CONT_PREFIX, keystr,
					(u_long)idl->b_ids[i] );
		}
	} else {
		const char *block_type;

		if ( NULL != keystr && *keystr == CONT_PREFIX ) {
			block_type = "Continued";
		} else {
			block_type = "Regular";
		}
		fprintf( outfp, "%s block (count=%lu, max=%lu)\n",
				block_type, (u_long)idl->b_nids, (u_long)idl->b_nmax );
		for ( i = 0; i < idl->b_nids; ++i ) {
			fprintf( outfp, "\t\tid: %lu\n", (u_long)idl->b_ids[i] );
		}
	}
}



#define BPLEN	48

static void
dbtest_bprint( char *data, int len, char *lineprefix, FILE *outfp )
{
	static char	hexdig[] = "0123456789abcdef";
	char		out[ BPLEN ], *curprefix;
	int		i = 0;

	if ( NULL == lineprefix ) {
		lineprefix = "";
	}
	curprefix = "";

	memset( out, 0, BPLEN );
	for ( ;; ) {
		if ( len < 1 ) {
			if ( i > 0 ) {
				fprintf( outfp, "%s%s\n", curprefix, out );
			}
			break;
		}

#ifndef HEX
		if ( isgraph( (unsigned char)*data )) {
			out[ i ] = ' ';
			out[ i+1 ] = *data;
		} else {
#endif
			out[ i ] = hexdig[ ( *data & 0xf0 ) >> 4 ];
			out[ i+1 ] = hexdig[ *data & 0x0f ];
#ifndef HEX
		}
#endif
		i += 2;
		len--;
		data++;

		if ( i > BPLEN - 2 ) {
			fprintf( outfp, "%s%s\n", curprefix, out );
			curprefix = lineprefix;
			memset( out, 0, BPLEN );
			i = 0;
			continue;
		}
		out[ i++ ] = ' ';
	}
}
