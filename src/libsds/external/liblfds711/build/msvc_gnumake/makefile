##### paths #####
BINDIR := ..\..\bin
INCDIR := ..\..\inc
OBJDIR := ..\..\obj
SRCDIR := ..\..\src

##### misc #####
QUIETLY := 1>nul 2>nul
NULL := 
SPACE := $(NULL) # TRD : with a trailing space

##### sources, objects and libraries #####
BINNAME    := liblfds711
LIB_BINARY := $(BINDIR)\$(BINNAME).lib
DLL_BINARY := $(BINDIR)\$(BINNAME).dll
SRCDIRS    := lfds711_btree_addonly_unbalanced lfds711_freelist lfds711_hash_addonly lfds711_list_addonly_singlylinked_ordered lfds711_list_addonly_singlylinked_unordered lfds711_misc lfds711_prng lfds711_queue_bounded_manyproducer_manyconsumer lfds711_queue_bounded_singleproducer_singleconsumer lfds711_queue_unbounded_manyproducer_manyconsumer lfds711_ringbuffer lfds711_stack
SOURCES    := lfds711_hash_addonly_cleanup.c lfds711_hash_addonly_get.c lfds711_hash_addonly_init.c lfds711_hash_addonly_insert.c lfds711_hash_addonly_iterate.c lfds711_hash_addonly_query.c \
              lfds711_list_addonly_singlylinked_ordered_cleanup.c lfds711_list_addonly_singlylinked_ordered_get.c lfds711_list_addonly_singlylinked_ordered_init.c lfds711_list_addonly_singlylinked_ordered_insert.c lfds711_list_addonly_singlylinked_ordered_query.c \
              lfds711_list_addonly_singlylinked_unordered_cleanup.c lfds711_list_addonly_singlylinked_unordered_get.c lfds711_list_addonly_singlylinked_unordered_init.c lfds711_list_addonly_singlylinked_unordered_insert.c lfds711_list_addonly_singlylinked_unordered_query.c \
              lfds711_btree_addonly_unbalanced_cleanup.c lfds711_btree_addonly_unbalanced_get.c lfds711_btree_addonly_unbalanced_init.c lfds711_btree_addonly_unbalanced_insert.c lfds711_btree_addonly_unbalanced_query.c \
              lfds711_freelist_cleanup.c lfds711_freelist_init.c lfds711_freelist_pop.c lfds711_freelist_push.c lfds711_freelist_query.c \
              lfds711_misc_internal_backoff_init.c lfds711_misc_globals.c lfds711_misc_query.c \
              lfds711_prng_init.c \
              lfds711_queue_bounded_manyproducer_manyconsumer_cleanup.c lfds711_queue_bounded_manyproducer_manyconsumer_dequeue.c lfds711_queue_bounded_manyproducer_manyconsumer_enqueue.c lfds711_queue_bounded_manyproducer_manyconsumer_init.c lfds711_queue_bounded_manyproducer_manyconsumer_query.c \
              lfds711_queue_bounded_singleproducer_singleconsumer_cleanup.c lfds711_queue_bounded_singleproducer_singleconsumer_dequeue.c lfds711_queue_bounded_singleproducer_singleconsumer_enqueue.c lfds711_queue_bounded_singleproducer_singleconsumer_init.c lfds711_queue_bounded_singleproducer_singleconsumer_query.c \
              lfds711_queue_unbounded_manyproducer_manyconsumer_cleanup.c lfds711_queue_unbounded_manyproducer_manyconsumer_dequeue.c lfds711_queue_unbounded_manyproducer_manyconsumer_enqueue.c lfds711_queue_unbounded_manyproducer_manyconsumer_init.c lfds711_queue_unbounded_manyproducer_manyconsumer_query.c \
              lfds711_ringbuffer_cleanup.c lfds711_ringbuffer_init.c lfds711_ringbuffer_query.c lfds711_ringbuffer_read.c lfds711_ringbuffer_write.c \
              lfds711_stack_cleanup.c lfds711_stack_init.c lfds711_stack_pop.c lfds711_stack_push.c lfds711_stack_query.c
OBJECTS    := $(patsubst %.c,$(OBJDIR)/%.obj,$(notdir $(SOURCES)))
SYSLIBS    := kernel32.lib

##### default paths fix up #####
INCDIRS := $(patsubst %,%;,$(INCDIR))
INCLUDE += $(subst $(SPACE),,$(INCDIRS))

##### tools #####
CC                    := cl
CFLAGS_MANDATORY      := /c "/Fd$(BINDIR)\$(BINNAME).pdb" /wd 4068
CFLAGS_OPTIONAL       := /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /nologo /W4 /WX
CFLAGS_MANDATORY_DBG  := /Od /Gm /Zi /D_DEBUG
CFLAGS_MANDATORY_REL  := /Ox /DNDEBUG

AR                    := lib
ARFLAGS               :=
ARFLAGS_MANDATORY     := /subsystem:console
ARFLAGS_OPTIONAL      := /nologo /wx /verbose

LD                    := link
LDFLAGS_MANDATORY     := /def:$(BINNAME).def /dll /nodefaultlib /subsystem:console
LDFLAGS_OPTIONAL      := /nologo /nxcompat /wx
LDFLAGS_MANDATORY_DBG := /debug "/pdb:$(BINDIR)\$(BINNAME).pdb"
LDFLAGS_MANDATORY_REL := /incremental:no

##### variants #####
ifeq ($(MAKECMDGOALS),) # TRD : default to debug lib
  CFLAGS_MANDATORY  += $(CFLAGS_MANDATORY_DBG)
  LDFLAGS_MANDATORY += $(LDFLAGS_MANDATORY_DBG)
  CLIB              := libcmtd.lib
endif

ifeq ($(MAKECMDGOALS),libdbg)
  CFLAGS_MANDATORY  += $(CFLAGS_MANDATORY_DBG)
  LDFLAGS_MANDATORY += $(LDFLAGS_MANDATORY_DBG)
  CLIB              := libcmtd.lib
endif

ifeq ($(MAKECMDGOALS),librel)
  CFLAGS_MANDATORY  += $(CFLAGS_MANDATORY_REL)
  LDFLAGS_MANDATORY += $(LDFLAGS_MANDATORY_REL)
  CLIB              := libcmt.lib
endif

ifeq ($(MAKECMDGOALS),dlldbg)
  CFLAGS_MANDATORY  += $(CFLAGS_MANDATORY_DBG)
  LDFLAGS_MANDATORY += $(LDFLAGS_MANDATORY_DBG)
  CLIB              := msvcrtd.lib
endif

ifeq ($(MAKECMDGOALS),dllrel)
  CFLAGS_MANDATORY  += $(CFLAGS_MANDATORY_REL)
  LDFLAGS_MANDATORY += $(LDFLAGS_MANDATORY_REL)
  CLIB              := msvcrt.lib
endif

##### search paths #####
vpath %.c $(patsubst %,$(SRCDIR)/%;,$(SRCDIRS))

##### implicit rules #####
$(OBJDIR)/%.obj : %.c
	$(CC) $(CFLAGS_OPTIONAL) $(CFLAGS) $(CFLAGS_MANDATORY) "/Fo$@" $<

##### explicit rules #####
$(LIB_BINARY) : $(OBJECTS)
	$(AR) $(ARFLAGS_OPTIONAL) $(ARFLAGS) $(ARFLAGS_MANDATORY) $(OBJECTS) /out:$(LIB_BINARY)

$(DLL_BINARY) : $(OBJECTS)
	$(LD) $(LDFLAGS_OPTIONAL) $(LDFLAGS) $(LDFLAGS_MANDATORY) $(CLIB) $(SYSLIBS) $(OBJECTS) /out:$(DLL_BINARY)

##### phony #####
.PHONY : clean librel libdbg dllrel dlldbg

clean : 
	@erase /Q $(BINDIR)\$(BINNAME).* $(OBJDIR)\*.obj $(QUIETLY)

dllrel : $(DLL_BINARY)
dlldbg : $(DLL_BINARY)

librel : $(LIB_BINARY)
libdbg : $(LIB_BINARY)

##### notes #####
# /wd 4068 : turn off "unknown pragma" warning

