/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include  <fcntl.h>
#include  <stdio.h>
#ifdef linux
#include <elf.h>
#include <libelf/libelf.h>
#else
#if defined(__osf__)
#include <elf_abi.h>
#else
#ifndef _AIX
#include  <libelf.h>
#endif
#endif
#endif
#include  <stdlib.h>
#include  <string.h>
#include <ctype.h>

#if defined(sparc) || defined(__sparc)
#include <sys/elf_SPARC.h>
#else
#if !defined(linux) && !defined(_AIX) && !defined(__osf__)
#include <sys/elf_386.h>
#endif
#endif


char *reldate = "23-APR-2002";

char *ofname = NULL;
FILE *of = NULL;

static void failure(char *s);

struct segment {
  char *vaddr;
  int len;
  char *physaddr;
};

struct segment segments[65536];
int segment_max;

char *modes[8] = { "---", "--X", "-W-", "-WX", "R--", "R-X", "RW-", "RWX" };

char *erf;
char *erf2;

char *find_string(char *addr,int l)
{
  int i,off,j;
  char *np;
  
  for (i = 0; i < segment_max;i++) {
    if (addr >= segments[i].vaddr && 
	addr <= (segments[i].vaddr + segments[i].len)) {
      off = addr - segments[i].vaddr;
      
      np = segments[i].physaddr + off;
      
      
      for (j = 0; j < 256; j++) {
	if (np[j] == '\0') {
	  return np;
	}
	if (!isascii(np[j])) {
	  break;
	}
      }
      if (l) {
	return("<bad pointer>");
      }
      return NULL;
    }
  }
  if (l) {
    return "<out of segment>";
  }
  return NULL;
}

struct iii_msgarray {
  unsigned int magic1;
  unsigned int magic2;
  unsigned int pos;
  unsigned int magic4;
  void *pointers[128][5];
} iii_msgarray;


void view_debug(char *cp)
{
  int i;
  char *s;
  char *s2;
  struct iii_msgarray *p = (struct iii_msgarray *)cp;

  for (i = 0; i < 128; i++) {
    int dl;
    int j,ap = 1;

    if (p->pointers[i][1] == NULL) continue;

    if (i == p->pos) {
      fprintf(of,"*");
    } else {
      fprintf(of," ");
    }

    dl = (int)p->pointers[i][0];
    switch(dl) {
    case 0:
      fprintf(of,"E ");
      break;
    case 1:
      fprintf(of,"  ");
      break;
    case 4:
      fprintf(of,"A ");
      break;
    case 8:
      fprintf(of,"C ");
      break;
    case 64:
      fprintf(of,"G ");
      break;
    case 4096:
      fprintf(of,"R ");
      break;
    case 0xffff:
      fprintf(of,"A ");
      break;
    default:
      fprintf(of," %5d ",p->pointers[i][0]);
    }
    
    s = find_string(p->pointers[i][1],1);
    
    for (j = 0; s[j] != '\0'; j++) {
      if (s[j] == '\n') break;
      if (s[j] == '%') {
	if (s[j+1] == 'l') j++;

	switch(s[j+1]) {
	case '%':
	default:
	  fprintf(of,"%c",s[j+1]);
	  break;
	case 'x':
	  ap++;
	  if (ap >= 5) {
	    fprintf(of,"(? (%%x)");
	  } else {
	    fprintf(of,"%x", p->pointers[i][ap]);
	  }
	  break;
	case 'd':
	  ap++;
	  if (ap >= 5) {
	    fprintf(of,"(? (%%d)");	    
	  } else {
	    fprintf(of,"%d", p->pointers[i][ap]);	    
	  }
	  break;
	case 's':
	  ap++;
	  if (ap >= 5) {
	    fprintf(of,"(? (%%s)");	    
	  } else {
	    s2 = find_string(p->pointers[i][ap],0);
	    if (s2) {
	      fprintf(of,"%s", s2);
	    } else {
	      fprintf(of,"(0x%x)", p->pointers[i][ap]);
	    }
	  }
	  break;
	}
	j++;
      } else {
	fprintf(of,"%c",s[j]);
      }
    }
    fprintf(of,"\n");
  }
}

int seek_debug(char *erf,int start, int mx)
{
  int i;
  unsigned int m1 = 0x0abbccdd;
  unsigned int m2 = 0xdeadbeef;
  
  for (i=start; i < mx; i+= sizeof(m1)) {
    if (memcmp(&erf[i],&m1,sizeof(m1)) == 0 && 
	memcmp(&erf[i+4],&m2,sizeof(m2)) == 0) {
      view_debug(&erf[i]);
      return 1;
    }
  }
  
  return 0;
}

#if !defined(_AIX) && !defined(__osf__)
void add_segment(Elf32_Phdr *phdr,char *base,int which)
{
  int i;

  i = segment_max;

  if (i >= 65536) {
    fprintf(of,"too many segments\n");
    exit(1);
  }
  i++;
  segment_max = i;
  
  segments[i].vaddr = (char *)phdr->p_vaddr;
  segments[i].len = phdr->p_filesz;
  segments[i].physaddr = base + phdr->p_offset;
}

void load_segments(Elf *elf,int phnum,char *base,int flen, int which)
{
  Elf32_Phdr *phdr;
  int i;

    
  phdr = elf32_getphdr(elf);
  
  if (phdr == NULL) {
    failure("getphdr");
  }
  
  /* printf("headers at %d\n",ehdr->e_phoff); */
  
  for (i = 0; i < phnum; i++) {
    if (phdr[i].p_type == PT_NOTE) {
      if (which) {
	fprintf(of,"%d: NOTE offset=0x%x filesz=0x%x flags=%d align=%d\n",
	       i, phdr[i].p_offset, 
	       phdr[i].p_filesz, 
	       phdr[i].p_flags, phdr[i].p_align);
      }
      
    } else if (phdr[i].p_type == PT_LOAD) {
      if (which) {
	fprintf(of,"%d: LOAD vaddr=0x%x filesz=0x%x memsz=0x%x mode=%s\n",
	       i, phdr[i].p_vaddr,
	       phdr[i].p_filesz, phdr[i].p_memsz,
	       modes[phdr[i].p_flags & 0x7]);
      }

      if (phdr[i].p_offset && phdr[i].p_filesz) {
	if (phdr[i].p_offset + phdr[i].p_filesz > flen) {
	  fprintf(of,"%d: segment out of range - core file is incomplete.\n",i);
	  continue;
	}
      }
      
      if ((phdr[i].p_flags == 7 || phdr[i].p_flags == 5) && phdr[i].p_filesz) {
	add_segment(&phdr[i],base,which);
      }
    } else {
      
      if (which) {
	fprintf(of,"%d: type=%d offset=0x%x vaddr=0x%x p=0x%x filesz=%d memsz=%d flags=%d align=%d\n",
	       i, phdr[i].p_type,phdr[i].p_offset, phdr[i].p_vaddr,
	       phdr[i].p_paddr, phdr[i].p_filesz, phdr[i].p_memsz,
	       phdr[i].p_flags, phdr[i].p_align);
      }
    }
  }
}

#endif

void try_adb(char *pf,char *cf)
{
  char buf[2048];
  FILE *p;

  if (ofname != NULL) {
    sprintf(buf,"/usr/bin/adb %s %s >>%s",pf,cf,ofname);
  } else {
    sprintf(buf,"/usr/bin/adb %s %s",pf,cf);
  }

  p = popen(buf,"w");

  if (p == NULL) {
    perror(buf);
    return;
  }

  fprintf(p,"$L\n");
  fprintf(p,"$m\n");
  fprintf(p,"$?\n");
  fprintf(p,"$C\n");
#ifdef notanymore
  fprintf(p,"sls_release_date /S\n");
  fprintf(p,"lash_release_date /S\n");
#endif

  fprintf(p,"$q\n");
  pclose(p);
}

void
main(int argc, char ** argv)
{
#if !defined(_AIX) && !defined(__osf__)
  Elf32_Ehdr *   ehdr;
  Elf32_Phdr *   phdr;
  Elf *          elf;
  Elf *          elf2;
  int       fd,fd2;
  int exf = 0;
  int i;
  size_t flen = 0;
  size_t flen2 = 0;
  time_t t;

  if (of == NULL) {
    of = stdout;
  }

  time(&t);

  if (argc != 4) {
    fprintf(of,"Core analysis %s needs three arguments: executable, core file, dest file\n",
	    reldate);
    exit(1);
  }
  
  ofname = argv[3];
  of = fopen(ofname,"a");
  
  fprintf(of,"Core analysis %s. Copyright 2001 Sun Microsystems, Inc.\nPortions copyright 1999, 2001-2003 Netscape Communications Corporation.\nAll rights reserved.\nCurrently %sOpening %s %s\n",reldate,ctime(&t),argv[1],argv[2]);

  if ((fd2 = open(argv[1], O_RDONLY)) == -1) {
    perror(argv[1]);
    exit(1);
  }
  (void) elf_version(EV_CURRENT);  
  /* Obtain the ELF descriptor */
  if ((elf2 = elf_begin(fd2, ELF_C_READ, NULL)) == NULL)
    failure("beginining");

  if ((erf2 = elf_rawfile(elf2,&flen2)) == NULL) {
    failure("elf_rawfile");
  }

  /* Obtain the .shstrtab data buffer */
  if ((ehdr = elf32_getehdr(elf2)) == NULL) {
    failure("reading header");
  }
  
  if (ehdr->e_type == ET_CORE) {
    fprintf(of,"Executable file should be given as the first argument.\n");
    exit(1);
  }
  
  if (ehdr->e_machine == EM_SPARC ||
      ehdr->e_machine == EM_SPARC32PLUS ||
      ehdr->e_machine == EM_SPARCV9) {
    fprintf(of,"architecture is SPARC\n");
    exf = EM_SPARC;
  } else if (ehdr->e_machine == EM_386) {
    fprintf(of,"architecture is x86\n");
    exf = ehdr->e_machine;
  } else {
    fprintf(of,"unknown architecture %d\n",ehdr->e_machine);
    exit(1);
  }
  

  if (ehdr->e_phnum == 0) {
    fprintf(of,"no program header in program file\n");
    exit(1);
  }

  load_segments(elf2,ehdr->e_phnum,erf2,flen2,0);

  /* Open the input file */
  if ((fd = open(argv[2], O_RDONLY)) == -1) {
    perror(argv[2]);
    exit(1);
  }
  
  /* Obtain the ELF descriptor */
  if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL)
    failure("beginining");

  if ((erf = elf_rawfile(elf,&flen)) == NULL) {
    failure("elf_rawfile");
  }

  /* Obtain the .shstrtab data buffer */
  if ((ehdr = elf32_getehdr(elf)) == NULL) {
    failure("reading header");
  }
  
  if (ehdr->e_type != ET_CORE) {
    fprintf(of,"second argument is ELF but not a core file\n");
    exit(1);
  }
  
  if (ehdr->e_machine != exf) {
    fprintf(of,"Architecture mismatch between executable and core file.\n");
    exit(1);
  }
  
  if (ehdr->e_phnum == 0) {
    fprintf(of,"no program header in core file\n");
    exit(1);
  }
  
  load_segments(elf,ehdr->e_phnum,erf,flen,1);
  

#ifdef notanymore
  fprintf(of,"Seeking debug information in core file.\n");

  phdr = elf32_getphdr(elf);
  
  for (i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      
      if (phdr[i].p_offset && phdr[i].p_filesz) {
	if (phdr[i].p_offset + phdr[i].p_filesz > flen) {
	  continue;
	}
	if (seek_debug(erf,phdr[i].p_offset,phdr[i].p_filesz)) {
	  break;
	}
      }
      
    }
  }
#endif

  if (of != stdout) {
    fclose(of);
  }

  try_adb(argv[1],argv[2]);

#endif  
}  

static void
failure(char *msg)
{
  fprintf(of, "%s: %s\n", msg, elf_errmsg(elf_errno()));
  exit(1);
}
