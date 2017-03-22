@echo off
rmdir /q /s single_dir_for_windows_kernel                                                                              1>nul 2>nul
mkdir single_dir_for_windows_kernel                                                                                    1>nul 2>nul

copy /y ..\..\src\lfds711_btree_addonly_unbalanced\*                     single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_freelist\*                                     single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_hash_addonly\*                                 single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_list_addonly_singlylinked_ordered\*            single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_list_addonly_singlylinked_unordered\*          single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_misc\*                                         single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_prng\*                                         single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_queue_bounded_manyproducer_manyconsumer\*      single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_queue_bounded_singleproducer_singleconsumer\*  single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_queue_unbounded_manyproducer_manyconsumer\*    single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_ringbuffer\*                                   single_dir_for_windows_kernel\                1>nul 2>nul
copy /y ..\..\src\lfds711_stack\*                                        single_dir_for_windows_kernel\                1>nul 2>nul

copy /y ..\..\src\liblfds711_internal.h                                  single_dir_for_windows_kernel\                1>nul 2>nul
copy /y driver_entry_renamed_to_avoid_compiler_warning.c                 single_dir_for_windows_kernel\driver_entry.c  1>nul 2>nul
copy /y sources.dynamic                                                  single_dir_for_windows_kernel\sources         1>nul 2>nul

echo Windows kernel dynamic library build directory structure created.
echo (Note the effects of this batch file are idempotent).

