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

#include "slap.h"	/* must come before regex.h */
#include "portable.h"

#if defined( MACOS ) || defined( DOS ) || defined( _WIN32 ) || defined( NEED_BSDREGEX )
#include "regex.h"

/*
 * regex - Regular expression pattern matching  and replacement
 *
 * By:  Ozan S. Yigit (oz)
 *      Dept. of Computer Science
 *      York University
 *
 * These routines are the PUBLIC DOMAIN equivalents of regex
 * routines as found in 4.nBSD UN*X, with minor extensions.
 *
 * These routines are derived from various implementations found
 * in software tools books, and Conroy's grep. They are NOT derived
 * from licensed/restricted software.
 * For more interesting/academic/complicated implementations,
 * see Henry Spencer's regexp routines, or GNU Emacs pattern
 * matching module.
 *
 * Modification history:
 *
 * $Log: regex.c,v $
 * Revision 1.5.2.1  2008/04/29 00:38:00  nhosoi
 * Resolves: #182621 (#443955)
 * Summary: Allow larger regex buffer to enable long substring filters
 * Description: Applying the patches provided by ulf.weltman@hp.com.
 * regex.c: use dynamically allocated regex buffer, use ptrdiff_t to store
 * the offsets to be restored after the realloc, and use a constant for the
 * value of "how much the NFA buffer can grow in one iteration on the pattern".
 * string.c: use dynamically allocated buffer if the prepared buffer is not
 * large enough, used wrong pointer (pat instead of p) in a debug message,
 * and performed an unneeded strcat of ".*"
 *
 * Revision 1.5  2006/11/10 23:45:40  nhosoi
 * Resolves: #214533
 * Summary: configure needs to support --with-fhs (Comment #6)
 * Changes: Added the following include next to the end of the copyright block.
 * +
 * +#ifdef HAVE_CONFIG_H
 * +#  include <config.h>
 * +#endif
 * +
 *
 * Revision 1.4  2005/04/19 22:07:37  nkinder
 * Fixed licensing typo
 *
 * Revision 1.3  2005/04/15 22:40:35  nkinder
 * 155068 - Added license to source files
 *
 * Revision 1.2  2005/02/28 23:38:00  nkinder
 * 149951 - Updated source code copyrights
 *
 * Revision 1.1.1.1  2005/01/21 00:40:51  cvsadm
 * Moving NSCP Directory Server from DirectoryBranch to TRUNK, initial drop. (foxworth)
 *
 * Revision 1.3.20.1.2.11  2005/01/14 01:22:12  nhosoi
 * For the open-source project.
 * 1) eliminated 'netsite' level
 * 2) moved ns/config one level lower
 * 3) moved fasttime to lib/base
 *
 * Revision 1.3.20.1.2.10  2004/10/01 18:46:09  dboreham
 * Rename the built in regex functions to avoid colliding with the native OS versions, where presnet
 *
 * Revision 1.2  2004/10/01 06:29:11  david
 * rename regex functions to avoid collision with native OS functions on Solaris
 *
 * Revision 1.1.1.1  2004/06/03 22:32:48  telackey
 * Initial import Thu Jun  3 15:32:43 PDT 2004
 *
 * Revision 1.3.20.1.2.9  2003/09/22 19:42:12  ulfw
 * Update copyright years from 2001 to 2001-2003
 *
 * Revision 1.3.20.1.2.8  2001/11/03 00:13:55  richm
 * XXX use new copyright XXX
 *
 * Revision 1.3.20.1.2.7  2001/10/07 00:59:03  richm
 * ldapserver/ldap/servers/slapd/regex.c
 * 1.3.20.1.2.7
 * 20010918
 *
 * Remove copyright caracter form copyright
 *
 *
 * ====================================================
 *
 * Revision 1.3.20.1.2.7  2001/09/18 11:43:06  rmarco
 * Remove copyright caracter form copyright
 *
 * Revision 1.3.20.1.2.6  2001/02/13 09:45:16  rmarco
 * copyrights
 *
 * Revision 1.3.20.1.2.5  1999/12/06 10:49:33  robey
 * fix gcc warning
 *
 * Revision 1.3.20.1.2.4  1999/11/18 01:26:26  robey
 * fix gcc warnings
 *
 * Revision 1.3.20.1.2.3  1999/08/20 23:13:33  merrells
 * tidy up extern things
 *
 * Revision 1.3.20.1.2.2  1999/05/19 23:41:58  merrells
 * Keep the Solaris compiler quiet
 *
 * Revision 1.3.20.1.2.1  1999/05/14 18:39:50  merrells
 * value manipulation code extraction and reworking.
 *
 * Revision 1.3.20.1  1998/10/10 02:28:25  ggood
 * Copy from Directory40RtmBranchpoint to DirectoryBranch
 *
 * Revision 1.3.10.5  1997/12/11 23:12:01  kristian
 * fix bugs 97502, 97504 & 96569: handle 8-bit char's (especially UTF-8) correctly.
 *
 * Revision 1.3.10.4  1997/12/07 22:13:08  howes
 * Always compile re_init(), re_lock(), and re_unlock(), even
 * on platforms where we use the native regex stuff.
 *
 * Revision 1.3.10.3  1997/12/07 00:01:16  howes
 * Add re_init(), re_lock(), and re_unlock() routines, to avoid race
 * between acl and syntax code.
 *
 * Revision 1.3.10.2  1997/07/17 07:49:13  mcs
 * merge changes made on ldapsdk_10_branch into server3_directory_branch
 *
 * Revision 1.3.10.1  1997/03/27 06:39:33  ggood
 * Fix up more compiler warnings.
 *
 * Revision 1.3  1996/11/07 00:44:44  mcs
 * eliminate a few compiler warnings
 *
 * Revision 1.2  1996/11/05 21:38:05  kristian
 * copied from Directory_1996_11_04
 *
 * Revision 1.1.1.1.2.1  1996/05/07 19:54:53  kristian
 * Merged UMich ldap-3_3 into Dogbert
 *
 * Revision 1.1.1.2  1996/05/04  19:11:02  kristian
 * UMich version 3.3
 *
 * Revision 1.2  1996/04/25  16:24:11  mcs
 * make re_exec() match "" with ".*" and similar patterns
 * hopefully this change doesn't break anything else!
 *
 * Revision 1.1  1995/02/03  15:56:52  tim
 * Initial revision
 *
 * Revision 1.11  1994/12/14  21:33:45  mcs
 * use new NEED_BSDREGEX
 * fix pmatch() prototype
 *
 * Revision 1.10  1994/12/12  18:16:39  mcs
 * use on NetBSD
 *
 * Revision 1.9  1994/11/15  19:16:35  mcs
 * add (CHAR) cast to make VisualC++ happy
 *
 * Revision 1.8  1994/11/08  21:14:32  mcs
 * WIN32 changes
 *
 * Revision 1.7  1994/07/23  19:51:24  mcs
 * use ANSI-style inline function parameters
 *
 * Revision 1.6  1993/10/18  01:52:32  tim
 * include for VMS
 *
 * Revision 1.5  1993/09/28  21:37:54  mcs
 * HP/UX needs the regex we include (not in its libc)
 *
 * Revision 1.4  1993/08/27  15:59:52  mcs
 * use CHAR for deftab
 *
 * Revision 1.3  1993/08/27  15:49:47  mcs
 * added missing 0 to octal constants
 * use unsigned char for CHAR under DOS
 *
 * Revision 1.2  1993/08/27  14:57:48  mcs
 * add proto. for pmatch
 *
 * Revision 1.1  1993/08/18  21:20:02  mcs
 * Initial revision
 *
 * Revision 1.4  1991/10/17  03:56:42  oz
 * miscellaneous changes, small cleanups etc.
 *
 * Revision 1.3  1989/04/01  14:18:09  oz
 * Change all references to a dfa: this is actually an nfa.
 *
 * Revision 1.2  88/08/28  15:36:04  oz
 * Use a complement bitmap to represent NCL.
 * This removes the need to have seperate 
 * code in the pmatch case block - it is 
 * just CCL code now.
 * 
 * Use the actual CCL code in the CLO
 * section of pmatch. No need for a recursive
 * pmatch call.
 * 
 * Use a bitmap table to set char bits in an
 * 8-bit chunk.
 * 
 * Interfaces:
 *	The following three interfaces were added to avoid a race
 *	condition in slapd. The better long-term solution is to make
 *	the regex code thread-safe, by passing in the buffers needed.
 *
 *	re_init:	initializes the regex system. must be called
 *			before any other regex calls are made.
 *
 *	re_lock:	locks the regex system. must be called to avoid
 *			races between calls to re_comp and re_exec.
 *
 *	re_unlock	unlocks the regex system. must be called after
 *			a set of calls to re_comp and re_exec.
 *
 *      re_comp:        compile a regular expression into a NFA.
 *
 *			char *re_comp(s)
 *			char *s;
 *
 *      re_exec:        execute the NFA to match a pattern.
 *
 *			int re_exec(s)
 *			char *s;
 *
 *	re_modw		change re_exec's understanding of what a "word"
 *			looks like (for \< and \>) by adding into the
 *			hidden word-syntax table.
 *
 *			void re_modw(s)
 *			char *s;
 *
 *      re_subs:	substitute the matched portions in a new string.
 *
 *			int re_subs(src, dst)
 *			char *src;
 *			char *dst;
 *
 *	re_fail:	failure routine for re_exec.
 *
 *			void re_fail(msg, op)
 *			char *msg;
 *			char op;
 *  
 * Regular Expressions:
 *
 *      [1]     char    matches itself, unless it is a special
 *                      character (metachar): . \ [ ] * + ^ $
 *
 *      [2]     .       matches any character.
 *
 *      [3]     \       matches the character following it, except
 *			when followed by a left or right round bracket,
 *			a digit 1 to 9 or a left or right angle bracket. 
 *			(see [7], [8] and [9])
 *			It is used as an escape character for all 
 *			other meta-characters, and itself. When used
 *			in a set ([4]), it is treated as an ordinary
 *			character.
 *
 *      [4]     [set]   matches one of the characters in the set.
 *                      If the first character in the set is "^",
 *                      it matches a character NOT in the set, i.e. 
 *			complements the set. A shorthand S-E is 
 *			used to specify a set of characters S upto 
 *			E, inclusive. The special characters "]" and 
 *			"-" have no special meaning if they appear 
 *			as the first chars in the set.
 *                      examples:        match:
 *
 *                              [a-z]    any lowercase alpha
 *
 *                              [^]-]    any char except ] and -
 *
 *                              [^A-Z]   any char except uppercase
 *                                       alpha
 *
 *                              [a-zA-Z] any alpha
 *
 *      [5]     *       any regular expression form [1] to [4], followed by
 *                      closure char (*) matches zero or more matches of
 *                      that form.
 *
 *      [6]     +       same as [5], except it matches one or more.
 *
 *      [7]             a regular expression in the form [1] to [10], enclosed
 *                      as \(form\) matches what form matches. The enclosure
 *                      creates a set of tags, used for [8] and for
 *                      pattern substution. The tagged forms are numbered
 *			starting from 1.
 *
 *      [8]             a \ followed by a digit 1 to 9 matches whatever a
 *                      previously tagged regular expression ([7]) matched.
 *
 *	[9]	\<	a regular expression starting with a \< construct
 *		\>	and/or ending with a \> construct, restricts the
 *			pattern matching to the beginning of a word, and/or
 *			the end of a word. A word is defined to be a character
 *			string beginning and/or ending with the characters
 *			A-Z a-z 0-9 and _. It must also be preceded and/or
 *			followed by any character outside those mentioned.
 *
 *      [10]            a composite regular expression xy where x and y
 *                      are in the form [1] to [10] matches the longest
 *                      match of x followed by a match for y.
 *
 *      [11]	^	a regular expression starting with a ^ character
 *		$	and/or ending with a $ character, restricts the
 *                      pattern matching to the beginning of the line,
 *                      or the end of line. [anchors] Elsewhere in the
 *			pattern, ^ and $ are treated as ordinary characters.
 *
 *
 * Acknowledgements:
 *
 *	HCR's Hugh Redelmeier has been most helpful in various
 *	stages of development. He convinced me to include BOW
 *	and EOW constructs, originally invented by Rob Pike at
 *	the University of Toronto.
 *
 * References:
 *              Software tools			Kernighan & Plauger
 *              Software tools in Pascal        Kernighan & Plauger
 *              Grep [rsx-11 C dist]            David Conroy
 *		ed - text editor		Un*x Programmer's Manual
 *		Advanced editing on Un*x	B. W. Kernighan
 *		RegExp routines			Henry Spencer
 *
 * Notes:
 *
 *	This implementation uses a bit-set representation for character
 *	classes for speed and compactness. Each character is represented 
 *	by one bit in a 128-bit block. Thus, CCL always takes a 
 *	constant 16 bytes in the internal nfa, and re_exec does a single
 *	bit comparison to locate the character in the set.
 *
 * Examples:
 *
 *	pattern:	foo*.*
 *	compile:	CHR f CHR o CLO CHR o END CLO ANY END END
 *	matches:	fo foo fooo foobar fobar foxx ...
 *
 *	pattern:	fo[ob]a[rz]	
 *	compile:	CHR f CHR o CCL bitset CHR a CCL bitset END
 *	matches:	fobar fooar fobaz fooaz
 *
 *	pattern:	foo\\+
 *	compile:	CHR f CHR o CHR o CHR \ CLO CHR \ END END
 *	matches:	foo\ foo\\ foo\\\  ...
 *
 *	pattern:	\(foo\)[1-3]\1	(same as foo[1-3]foo)
 *	compile:	BOT 1 CHR f CHR o CHR o EOT 1 CCL bitset REF 1 END
 *	matches:	foo1foo foo2foo foo3foo
 *
 *	pattern:	\(fo.*\)-\1
 *	compile:	BOT 1 CHR f CHR o CLO ANY END EOT 1 CHR - REF 1 END
 *	matches:	foo-foo fo-fo fob-fob foobar-foobar ...
 */

/* This is the maximum the NFA buffer might grow for every op code processed.
   The max seems to be the + after a character class, like "[a-z]+".  It
   needs 1 byte for the CCL code, 16 for the CCL bit map, and 2 for END codes
   and 1 for a CLO code. */
#define MAXOPSPACE 20

#define MAXNFA  1024
#define MAXTAG  10

#define OKP     1
#define NOP     0

#define CHR     1
#define ANY     2
#define CCL     3
#define BOL     4
#define EOL     5
#define BOT     6
#define EOT     7
#define BOW	8
#define EOW	9
#define REF     10
#define CLO     11

#define END     0

/*
 * The following defines are not meant to be changeable.
 * They are for readability only.
 */
#define MAXCHR	128
#define CHRBIT	8
#define BITBLK	MAXCHR/CHRBIT
#define BLKIND	0170
#define BITIND	07

#define ASCIIB	0177

typedef unsigned char UCHAR;
/* char, on the other hand, may be signed or unsigned;
 * it's platform-dependent.  A hard fact of life, in C.
 */

static int  tagstk[MAXTAG];             /* subpat tag stack..*/
static UCHAR *nfa = NULL;               /* automaton..       */
static int  nfasize = MAXNFA;           /* tracks size of nfa buffer */
static int  sta = NOP;                  /* status of lastpat */

static UCHAR bittab[BITBLK];            /* bit table for CCL */
                                        /* pre-set bits...   */
static UCHAR bitarr[] = {1,2,4,8,16,32,64,128};

#ifdef DEBUG
static void nfadump( UCHAR *ap);
#endif

static void
chset(UCHAR c)
{
	bittab[((c) & (unsigned)BLKIND) >> 3] |= bitarr[(c) & BITIND];
}

#define badpat(x)	(*nfa = END, x)
#define store(x)	*mp++ = x
 
char *     
slapd_re_comp( char *pat )
{
	register UCHAR *p;               /* pattern pointer   */
	register UCHAR *mp=nfa;          /* nfa pointer       */
	register UCHAR *lp;              /* saved pointer..   */
	register UCHAR *sp=nfa;          /* another one..     */

	register int tagi = 0;          /* tag stack index   */
	register int tagc = 1;          /* actual tag count  */

	register int n;
	register UCHAR mask;		/* xor mask -CCL/NCL */
	int c1, c2;
		
	if (!pat || !*pat) {
		if (sta)
			return 0;
		else
			return badpat("No previous regular expression");
        }
	sta = NOP;

	for (p = (UCHAR*)pat; *p; p++) {
		/* Check if we are approaching end of nfa buffer. MAXOPSPACE is
		   the max we might add to the nfa per loop. */
		if (mp - (UCHAR*)nfa + MAXOPSPACE >= nfasize) {
			/* Save offsets */
			ptrdiff_t mppos = mp - nfa;
			ptrdiff_t sppos = sp - nfa;

			/* Double the nfa buffer size */
			nfasize *= 2;
			nfa = (UCHAR*)slapi_ch_realloc((char*)nfa, nfasize);

			/* Restore pointers into realloced space */
			mp = nfa + mppos;
			sp = nfa + sppos;
		}
		lp = mp;
		switch(*p) {

		case '.':               /* match any char..  */
			store(ANY);
			break;

		case '^':               /* match beginning.. */
			if (p == (UCHAR*)pat)
				store(BOL);
			else {
				store(CHR);
				store(*p);
			}
			break;

		case '$':               /* match endofline.. */
			if (!*(p+1))
				store(EOL);
			else {
				store(CHR);
				store(*p);
			}
			break;

		case '[':               /* match char class..*/
			store(CCL);

			if (*++p == '^') {
				mask = 0377;	
				p++;
			}
			else
				mask = 0;

			if (*p == '-')		/* real dash */
				chset(*p++);
			if (*p == ']')		/* real brac */
				chset(*p++);
			while (*p && *p != ']') {
				if (*p == '-' && *(p+1) && *(p+1) != ']') {
					p++;
					c1 = *(p-2) + 1;
					c2 = *p++;
					while (c1 <= c2)
						chset((UCHAR)c1++);
				}
#ifdef EXTEND
				else if (*p == '\\' && *(p+1)) {
					p++;
					chset(*p++);
				}
#endif
				else
					chset(*p++);
			}
			if (!*p)
				return badpat("Missing ]");

			for (n = 0; n < BITBLK; bittab[n++] = (UCHAR) 0)
				store(mask ^ bittab[n]);
	
			break;

		case '*':               /* match 0 or more.. */
		case '+':               /* match 1 or more.. */
			if (p == (UCHAR*)pat)
				return badpat("Empty closure");
			lp = sp;		/* previous opcode */
			if (*lp == CLO)		/* equivalence..   */
				break;
			switch(*lp) {

			case BOL:
			case BOT:
			case EOT:
			case BOW:
			case EOW:
			case REF:
				return badpat("Illegal closure");
			default:
				break;
			}

			if (*p == '+')
				for (sp = mp; lp < sp; lp++)
					store(*lp);

			store(END);
			store(END);
			sp = mp;
			while (--mp > lp)
				*mp = mp[-1];
			store(CLO);
			mp = sp;
			break;

		case '\\':              /* tags, backrefs .. */
			switch(*++p) {

			case '(':
				if (tagc < MAXTAG) {
					tagstk[++tagi] = tagc;
					store(BOT);
					store(tagc++);
				}
				else
					return badpat("Too many \\(\\) pairs");
				break;
			case ')':
				if (*sp == BOT)
					return badpat("Null pattern inside \\(\\)");
				if (tagi > 0) {
					store(EOT);
					store(tagstk[tagi--]);
				}
				else
					return badpat("Unmatched \\)");
				break;
			case '<':
				store(BOW);
				break;
			case '>':
				if (*sp == BOW)
					return badpat("Null pattern inside \\<\\>");
				store(EOW);
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				n = *p-'0';
				if (tagi > 0 && tagstk[tagi] == n)
					return badpat("Cyclical reference");
				if (tagc > n) {
					store(REF);
					store(n);
				}
				else
					return badpat("Undetermined reference");
				break;
#ifdef EXTEND
			case 'b':
				store(CHR);
				store('\b');
				break;
			case 'n':
				store(CHR);
				store('\n');
				break;
			case 'f':
				store(CHR);
				store('\f');
				break;
			case 'r':
				store(CHR);
				store('\r');
				break;
			case 't':
				store(CHR);
				store('\t');
				break;
#endif
			default:
				store(CHR);
				store(*p);
			}
			break;

		default :               /* an ordinary char  */
			store(CHR);
			store(*p);
			break;
		}
		sp = lp;
	}
	if (tagi > 0)
		return badpat("Unmatched \\(");
	store(END);
	sta = OKP;
	return 0;
}


static UCHAR *bol;
static UCHAR *bopat[MAXTAG];
static UCHAR *eopat[MAXTAG];
#ifdef NEEDPROTOS
static UCHAR *pmatch( UCHAR *lp, UCHAR *ap );
#else /* NEEDPROTOS */
static UCHAR *pmatch();
#endif /* NEEDPROTOS */

/*
 * re_exec:
 * 	execute nfa to find a match.
 *
 *	special cases: (nfa[0])	
 *		BOL
 *			Match only once, starting from the
 *			beginning.
 *		CHR
 *			First locate the character without
 *			calling pmatch, and if found, call
 *			pmatch for the remaining string.
 *		END
 *			re_comp failed, poor luser did not
 *			check for it. Fail fast.
 *
 *	If a match is found, bopat[0] and eopat[0] are set
 *	to the beginning and the end of the matched fragment,
 *	respectively.
 *
 */

int
slapd_re_exec( char *lp )
{
	register UCHAR c;
	register UCHAR *ep = 0;
	register UCHAR *ap = nfa;

	bol = (UCHAR*)lp;

	bopat[0] = 0;
	bopat[1] = 0;
	bopat[2] = 0;
	bopat[3] = 0;
	bopat[4] = 0;
	bopat[5] = 0;
	bopat[6] = 0;
	bopat[7] = 0;
	bopat[8] = 0;
	bopat[9] = 0;

	switch(*ap) {

	case BOL:			/* anchored: match from BOL only */
		ep = pmatch((UCHAR*)lp,ap);
		break;
	case CHR:			/* ordinary char: locate it fast */
		c = *(ap+1);
		while (*lp && *(UCHAR*)lp != c)
			lp++;
		if (!*lp)		/* if EOS, fail, else fall thru. */
			return 0;
	default:			/* regular matching all the way. */
		do {
			if ((ep = pmatch((UCHAR*)lp,ap)))
				break;
			lp++;
		} while (*lp);

		break;
	case END:			/* munged automaton. fail always */
		return 0;
	}
	if (!ep)
		return 0;

	bopat[0] = (UCHAR*)lp;
	eopat[0] = ep;
	return 1;
}

/* 
 * pmatch: internal routine for the hard part
 *
 * 	This code is partly snarfed from an early grep written by
 *	David Conroy. The backref and tag stuff, and various other
 *	innovations are by oz.
 *
 *	special case optimizations: (nfa[n], nfa[n+1])
 *		CLO ANY
 *			We KNOW .* will match everything upto the
 *			end of line. Thus, directly go to the end of
 *			line, without recursive pmatch calls. As in
 *			the other closure cases, the remaining pattern
 *			must be matched by moving backwards on the
 *			string recursively, to find a match for xy
 *			(x is ".*" and y is the remaining pattern)
 *			where the match satisfies the LONGEST match for
 *			x followed by a match for y.
 *		CLO CHR
 *			We can again scan the string forward for the
 *			single char and at the point of failure, we
 *			execute the remaining nfa recursively, same as
 *			above.
 *
 *	At the end of a successful match, bopat[n] and eopat[n]
 *	are set to the beginning and end of subpatterns matched
 *	by tagged expressions (n = 1 to 9).	
 *
 */

#ifndef re_fail
void re_fail();
#endif /* re_fail */

/*
 * character classification table for word boundary operators BOW
 * and EOW. the reason for not using ctype macros is that we can
 * let the user add into our own table. see re_modw. This table
 * is not in the bitset form, since we may wish to extend it in the
 * future for other character classifications. 
 *
 *	TRUE for 0-9 A-Z a-z _
 */
static char chrtyp[MAXCHR] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 1, 0, 0, 0, 0, 0
	};

#define inascii(x)	(0177&(x))
#define iswordc(x) 	chrtyp[inascii(x)]
#define isinset(x,y) 	((x)[((y)&BLKIND)>>3] & bitarr[(y)&BITIND])

/*
 * skip values for CLO XXX to skip past the closure
 */

#define ANYSKIP	2 	/* [CLO] ANY END ...	     */
#define CHRSKIP	3	/* [CLO] CHR chr END ...     */
#define CCLSKIP 18	/* [CLO] CCL 16bytes END ... */

static UCHAR *
pmatch( UCHAR *lp, UCHAR *ap)
{
	register int op, c, n;
	register UCHAR *e;		/* extra pointer for CLO */
	register UCHAR *bp;		/* beginning of subpat.. */
	register UCHAR *ep;		/* ending of subpat..	 */
	UCHAR *are;			/* to save the line ptr. */

	while ((op = *ap++) != END)
		switch(op) {

		case CHR:
			if (*lp++ != *ap++)
				return 0;
			break;
		case ANY:
			if (!*lp++)
				return 0;
			break;
		case CCL:
			c = *lp++;
			if (!isinset(ap,c))
				return 0;
			ap += BITBLK;
			break;
		case BOL:
			if (lp != bol)
				return 0;
			break;
		case EOL:
			if (*lp)
				return 0;
			break;
		case BOT:
			bopat[*ap++] = lp;
			break;
		case EOT:
			eopat[*ap++] = lp;
			break;
 		case BOW:
			if ((lp!=bol && iswordc(lp[-1])) || !iswordc(*lp))
				return 0;
			break;
		case EOW:
			if (lp==bol || !iswordc(lp[-1]) || iswordc(*lp))
				return 0;
			break;
		case REF:
			n = *ap++;
			bp = bopat[n];
			ep = eopat[n];
			while (bp < ep)
				if (*bp++ != *lp++)
					return 0;
			break;
		case CLO:
			are = lp;
			switch(*ap) {

			case ANY:
				while (*lp)
					lp++;
				n = ANYSKIP;
				break;
			case CHR:
				c = *(ap+1);
				while (*lp && c == *lp)
					lp++;
				n = CHRSKIP;
				break;
			case CCL:
				while ((c = *lp) && isinset(ap+1,c))
					lp++;
				n = CCLSKIP;
				break;
			default:
				re_fail("closure: bad nfa.", *ap);
				return 0;
			}

			ap += n;

			while (lp >= are) {
				if ((e = pmatch(lp, ap)) != NULL)
					return e;
				--lp;
			}
			return 0;
		default:
			re_fail("re_exec: bad nfa.", op);
			return 0;
		}
	return lp;
}

/*
 * re_modw:
 *	add new characters into the word table to change re_exec's
 *	understanding of what a word should look like. Note that we
 *	only accept additions into the word definition.
 *
 *	If the string parameter is 0 or null string, the table is
 *	reset back to the default containing A-Z a-z 0-9 _. [We use
 *	the compact bitset representation for the default table]
 */

static UCHAR deftab[16] = {	
	0, 0, 0, 0, 0, 0, 0377, 003, 0376, 0377, 0377, 0207,  
	0376, 0377, 0377, 007 
}; 

void
slapd_re_modw( char *s )
{
	register int i;

	if (!s || !*s) {
		for (i = 0; i < MAXCHR; i++)
			if (!isinset(deftab,i))
				iswordc(i) = 0;
	}
	else
		while(*s)
			iswordc(*s++) = 1;
}

/*
 * re_subs:
 *	substitute the matched portions of the src in dst.
 *
 *	&	substitute the entire matched pattern.
 *
 *	\digit	substitute a subpattern, with the given	tag number.
 *		Tags are numbered from 1 to 9. If the particular
 *		tagged subpattern does not exist, null is substituted.
 */
int
slapd_re_subs( char *src, char *dst)
{
	register char c;
	register int  pin;
	register UCHAR *bp;
	register UCHAR *ep;

	if (!*src || !bopat[0])
		return 0;

	while ((c = *src++) != 0) {
		switch(c) {

		case '&':
			pin = 0;
			break;

		case '\\':
			c = *src++;
			if (c >= '0' && c <= '9') {
				pin = c - '0';
				break;
			}
			
		default:
			*dst++ = c;
			continue;
		}

		if ((bp = bopat[pin]) && (ep = eopat[pin])) {
			while (*bp && bp < ep)
				*dst++ = *(char*)bp++;
			if (bp < ep)
				return 0;
		}
	}
	*dst = (char) 0;
	return 1;
}
			
#ifdef DEBUG
/*
 * symbolic - produce a symbolic dump of the nfa
 */
void
symbolic( char *s ) 
{
	printf("pattern: %s\n", s);
	printf("nfacode:\n");
	nfadump(nfa);
}

static void
nfadump( UCHAR *ap)
{
	register int n;

	while (*ap != END)
		switch(*ap++) {
		case CLO:
			printf("CLOSURE");
			nfadump(ap);
			switch(*ap) {
			case CHR:
				n = CHRSKIP;
				break;
			case ANY:
				n = ANYSKIP;
				break;
			case CCL:
				n = CCLSKIP;
				break;
			}
			ap += n;
			break;
		case CHR:
			printf("\tCHR %c\n",*ap++);
			break;
		case ANY:
			printf("\tANY .\n");
			break;
		case BOL:
			printf("\tBOL -\n");
			break;
		case EOL:
			printf("\tEOL -\n");
			break;
		case BOT:
			printf("BOT: %d\n",*ap++);
			break;
		case EOT:
			printf("EOT: %d\n",*ap++);
			break;
		case BOW:
			printf("BOW\n");
			break;
		case EOW:
			printf("EOW\n");
			break;
		case REF:
			printf("REF: %d\n",*ap++);
			break;
		case CCL:
			printf("\tCCL [");
			for (n = 0; n < MAXCHR; n++)
				if (isinset(ap,(UCHAR)n)) {
					if (n < ' ')
						printf("^%c", n ^ 0x040);
					else
						printf("%c", n);
				}
			printf("]\n");
			ap += BITBLK;
			break;
		default:
			printf("bad nfa. opcode %o\n", ap[-1]);
			exit(1);
			break;
		}
}
#endif
#endif /* MACOS or DOS or NEED_BSDREGEX */

static PRLock	*regex_mutex = NULL;

int
slapd_re_init( void )
{
	if ( NULL == regex_mutex ) {
		regex_mutex = PR_NewLock();
	}

	if ( NULL == nfa ) {
		nfa = (UCHAR*)slapi_ch_malloc( MAXNFA );
	}

	return( NULL == regex_mutex ? -1 : 0 );
}

void
slapd_re_lock( void )
{
	PR_ASSERT( NULL != regex_mutex );
	PR_Lock( regex_mutex );
}

int
slapd_re_unlock( void )
{
	PR_ASSERT( NULL != regex_mutex );
	return( PR_Unlock( regex_mutex ) );
}
