/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _avaparsedfiles_h_
#define _avaparsedfiles_h_

#include "libaccess/ava.h"
#include "frame/req.h"
#include "base/session.h"

#define AUTH_DB_FILE "AvaCertmap"
#define AVADB_TAG    "avadb"
#define AVA_DB_SEL   "ava_db_sel" /*Variable name used in
				   *outputAVAdbs
				   */


extern void outputAVAdbs (char *chosen); /*Outputs the selector of auth databases
					  *and makes it so that the form submits 
					  *when onChange event occurs. 
					  */


/*For the following 3 functions, enter the full path of 
 *ava database file includint tag and filename
 */
/*Before calling _getTable, initializa yy_sn and yy_rq.  Set to NULL if no
 *Session* or Request* variables exist and an error will be reported with 
 *function report_error(libamin.h).  Otherwise error will be logged into
 *the server's error log
 */
extern AVATable *_getTable (char *avadbfile);
extern AVATable *_wasParsed (char *avadbfile);/*Assumes a call to yyparse was just
					       *completed
					       */
extern int _hasBeenParsed (char *avadbfile);/*Check if _getTable returns NULL or not*/

extern AVAEntry* _getAVAEntry (char *groupid, AVATable *table);
extern AVAEntry* _deleteAVAEntry (char *groupid, AVATable *table);
extern void _addAVAtoTable (AVAEntry *entry, AVATable *table);
extern void AVAEntry_Free (AVAEntry *entry);

/*Functions for writing out files*/
extern void PrintHeader (FILE *outfile);
extern void writeOutFile (char *avadbfilename, AVATable *table);


extern int yyparse();
extern FILE *yyin;

extern char *currFile;

extern Session *yy_sn;
extern Request *yy_rq;


#endif /*_avaparsedfiles_h_*/
