/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Originally by Linus Torvalds.
 * Smart CONFIG_* processing by Werner Almesberger, Michael Chastain.
 * Lobotomized by Robey Pointer.
 *
 * Usage: mkdep file ...
 *
 * Read source files and output makefile dependency lines for them.
 * I make simple dependency lines for #include "*.h".
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


char __depname[512] = "\n\t@touch ";
#define depname (__depname + 9)
int hasdep;

char *outdir = ".";

struct path_struct
{
    int len;
    char buffer[256 - sizeof(int)];
} path = {0, ""};

#define EXISTS(_fn) access(_fn, F_OK)

/*
 * Handle an #include line.
 */
void
handle_include(const char *name, int len)
{
    memcpy(path.buffer + path.len, name, len);
    path.buffer[path.len + len] = '\0';
    if (EXISTS(path.buffer))
        return;

    if (!hasdep) {
        hasdep = 1;
        /* don't use outdir if it's a .h file */
        if ((strlen(depname) > 2) &&
            (strcmp(depname + strlen(depname) - 2, ".h") == 0)) {
            /* skip using the outdir */
        } else {
            if (outdir)
                printf("%s/", outdir);
        }
        printf("%s:", depname);
    }
    printf(" \\\n   %s", path.buffer);
}


/* --- removed weird functions to try to emulate asm ---
 * (turns out it's faster just to scan thru a char*)
 */

#define GETNEXT            \
    {                      \
        current = *next++; \
        if (next >= end)   \
            break;         \
    }

/*
 * State machine macros.
 */
#define CASE(c, label) \
    if (current == c)  \
    goto label
#define NOTCASE(c, label) \
    if (current != c)     \
    goto label

/*
 * Yet another state machine speedup.
 */
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX4(a, b, c, d) (MAX2(a, MAX2(b, MAX2(c, d))))
#define MIN4(a, b, c, d) (MIN2(a, MIN2(b, MIN2(c, d))))


/*
 * The state machine looks for (approximately) these Perl regular expressions:
 *
 *    m|\/\*.*?\*\/|
 *    m|'.*?'|
 *    m|".*?"|
 *    m|#\s*include\s*"(.*?)"|
 *
 * About 98% of the CPU time is spent here, and most of that is in
 * the 'start' paragraph.  Because the current characters are
 * in a register, the start loop usually eats 4 or 8 characters
 * per memory read.  The MAX6 and MIN6 tests dispose of most
 * input characters with 1 or 2 comparisons.
 */
void
state_machine(const char *map, const char *end)
{
    register const char *next = map;
    register const char *map_dot;
    register unsigned char current;

    for (;;) {
    start:
        GETNEXT
    __start:
        if (current > MAX4('/', '\'', '"', '#'))
            goto start;
        if (current < MIN4('/', '\'', '"', '#'))
            goto start;
        CASE('/', slash);
        CASE('\'', squote);
        CASE('"', dquote);
        CASE('#', pound);
        goto start;

    /* / */
    slash:
        GETNEXT
        NOTCASE('*', __start);
    slash_star_dot_star:
        GETNEXT
    __slash_star_dot_star:
        NOTCASE('*', slash_star_dot_star);
        GETNEXT
        NOTCASE('/', __slash_star_dot_star);
        goto start;

    /* '.*?' */
    squote:
        GETNEXT
        CASE('\'', start);
        NOTCASE('\\', squote);
        GETNEXT
        goto squote;

    /* ".*?" */
    dquote:
        GETNEXT
        CASE('"', start);
        NOTCASE('\\', dquote);
        GETNEXT
        goto dquote;

    /* #\s* */
    pound:
        GETNEXT
        CASE(' ', pound);
        CASE('\t', pound);
        CASE('i', pound_i);
        goto __start;

    /* #\s*i */
    pound_i:
        GETNEXT NOTCASE('n', __start);
        GETNEXT NOTCASE('c', __start);
        GETNEXT NOTCASE('l', __start);
        GETNEXT NOTCASE('u', __start);
        GETNEXT NOTCASE('d', __start);
        GETNEXT NOTCASE('e', __start);
        goto pound_include;

    /* #\s*include\s* */
    pound_include:
        GETNEXT
        CASE(' ', pound_include);
        CASE('\t', pound_include);
        map_dot = next;
        CASE('"', pound_include_dquote);
        goto __start;

    /* #\s*include\s*"(.*)" */
    pound_include_dquote:
        GETNEXT
        CASE('\n', start);
        NOTCASE('"', pound_include_dquote);
        handle_include(map_dot, next - map_dot - 1);
        goto start;
    }
}

/*
 * Generate dependencies for one file.
 */
void
do_depend(const char *filename, const char *command)
{
    int mapsize;
    int pagesizem1 = getpagesize() - 1;
    int fd;
    struct stat st;
    char *map;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror(filename);
        return;
    }

    fstat(fd, &st);
    if (st.st_size == 0) {
        fprintf(stderr, "%s is empty\n", filename);
        close(fd);
        return;
    }

    mapsize = st.st_size;
    mapsize = (mapsize + pagesizem1) & ~pagesizem1;
    map = mmap(NULL, mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if ((long)map == -1) {
        perror("mkdep: mmap");
        close(fd);
        return;
    }
    if ((unsigned long)map % sizeof(unsigned long) != 0) {
        fprintf(stderr, "do_depend: map not aligned\n");
        exit(1);
    }

    hasdep = 0;
    state_machine(map, map + st.st_size);
    if (hasdep)
        puts(command);

    munmap(map, mapsize);
    close(fd);
}

/*
 * Generate dependencies for all files.
 */
int
main(int argc, char **argv)
{
    int len;

    while (--argc > 0) {
        const char *filename = *++argv;
        const char *command = __depname;

        if (strcmp(filename, "-o") == 0) {
            outdir = *++argv;
            argc--;
            continue;
        }
        len = strlen(filename);
        memcpy(depname, filename, len + 1);
        if (len > 2 && filename[len - 2] == '.') {
            if (filename[len - 1] == 'c' || filename[len - 1] == 'S') {
                depname[len - 1] = 'o';
                command = "";
            }
        }
        do_depend(filename, command);
    }
    return 0;
}
