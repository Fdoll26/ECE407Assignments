ECE 407 Assignment Report : Memory Problems
Group Name/Number : Pop Bob Moments
References
[1] GeeksforGeeks, “What is Memory Leak? How can we avoid?,” GeeksforGeeks, Feb. 06, 2010. https://www.geeksforgeeks.org/c/what-is-memory-leak-how-can-we-avoid/
[2] “How To Find And Fix Memory Leaks in C or C++ | Netdata,” Netdata.cloud, 2026. https://www.netdata.cloud/academy/how-to-find-memory-leak-in-c/ (accessed Feb. 28, 2026).
[3] M. Mishra, “Memory Leak In C | Find, Avoid & Fix (With Code Examples),” Unstop.com, Jul. 30, 2024. https://unstop.com/blog/memory-leak-in-c

<Marks : 5

There really should be references, even if only the lecture notes, but any website used for guidance, reference, or tutorials should be included>

Purpose
The purpose of this assignment is to explore debugging tools against common hardware development errors. Testing software on hardware has expectations of what we see out of it, and when things go wrong there is two regions, to check, the first is the wiring of the hardware and the second is the software implementation. Using debugging tools like GDB is the first step to find problems, and the second is memory tools to find unreclaimed regions that occur. Through this assignment, exploring valgrind and its usage furthers knowledge and use of the tool in the future.

Introduction
Memory management is critical in embedded systems because often times resources are limited and errors can lead to system crashes or undefined behavior. C requires manual allocation and deallocation of memory. Improper handling can result in memory leaks, dangling pointers, or buffer overflows.

Pico Memory
The program memory.c was compiled for the Pico so that the storage organization of the Pico for memory.c can be investigated.

The Pico memory layout was examined using:
 - pico_flash_region.ld
 - memory.dis

The linker script defines:
 - Flash region
 - RAM region
 - Stack placement
 - Heap placement

The .text section resides in flash memory, while .data and .bss are loaded into RAM. Heap grows upward and stack grows downward.
This is relevant because:
 - Buffer overflow can corrupt stack memory.
 - Memory leaks consume heap space.
 - Embedded systems have strict memory limits, so leaks are critical.

<Marks : 5>

C Program with memory problems
1: Lost Ref
 - GDB Analysis
    9           *ptr = 123;
    (gdb) print prt
    No symbol "prt" in current context.
    (gdb) print ptr
    $2 = (int *) 0x5555555592a0
    (gdb) next
    10          ptr = NULL;
    (gdb) print ptr
    $3 = (int *) 0x5555555592a0
    (gdb) next
    11      }
    (gdb) ptr
    Undefined command: "ptr".  Try "help".
    (gdb) print ptr
    $4 = (int *) 0x0
 - Valgrind Result
    ==43952== LEAK SUMMARY:
    ==43952==    definitely lost: 4 bytes in 1 blocks
    ==43952==    indirectly lost: 0 bytes in 0 blocks
    ==43952==      possibly lost: 0 bytes in 0 blocks
    ==43952==    still reachable: 0 bytes in 0 blocks
    ==43952==         suppressed: 0 bytes in 0 blocks
 - Prevention: Before reassigning a pointer to a new address, free the memory address

2: Pointer No Free
 - GDB Analysis
    (gdb) next
    17          ptr[0] = 42;
    (gdb) print ptr
    $2 = (int *) 0x5555555592a0
    (gdb) next
    19          ptr = (int *)malloc(20 * sizeof(int));
    (gdb) print ptr
    $3 = (int *) 0x5555555592a0
    (gdb) next
    20          if (!ptr) return;
    (gdb) print ptr
    $4 = (int *) 0x5555555592d0
 - Valgrind Result
    ==44460== LEAK SUMMARY:
    ==44460==    definitely lost: 120 bytes in 2 blocks
    ==44460==    indirectly lost: 0 bytes in 0 blocks
    ==44460==      possibly lost: 0 bytes in 0 blocks
    ==44460==    still reachable: 0 bytes in 0 blocks
    ==44460==         suppressed: 0 bytes in 0 blocks
 - Prevention: Free memory before overwriting pointer with new malloc data

3: Early Return
 - GDB Analysis
    (gdb) next
    30          strcpy(ptr, "allocated then returned early");
    (gdb) print ptr
    $3 = 0x5555555592a0 ""
    (gdb) next
    32          if (error_condition) return;
    (gdb) print ptr
    $4 = 0x5555555592a0 "allocated then returned early"
    (gdb) next
    34      }
    (gdb) print ptr
    $5 = 0x5555555592a0 "allocated then returned early"
    (gdb) next
    main (argc=3, argv=0x7fffffffd908) at memory.c:123
    123             case 3: early_return(); break;
    (gdb) print ptr
    No symbol "ptr" in current context.
 - Valgrind Result
    ==44582== LEAK SUMMARY:
    ==44582==    definitely lost: 100 bytes in 1 blocks
    ==44582==    indirectly lost: 0 bytes in 0 blocks
    ==44582==      possibly lost: 0 bytes in 0 blocks
    ==44582==    still reachable: 0 bytes in 0 blocks
    ==44582==         suppressed: 0 bytes in 0 blocks
 - Prevention: When doing returns from a function, ensure that all allocated memory is cleared before returning

4: Loop No Free
 - GDB Analysis
    Breakpoint 1, loop_no_free () at memory.c:37
    37          for (int i = 0; i < 1000; i++) {
    (gdb) next
    38              int *temp = (int *)malloc(sizeof(int));
    (gdb) break malloc
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./string/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    --Type <RET> for more, q to quit, c to continue without paging--c
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/inline-hashtab.h.
    Download failed: Invalid argument.  Continuing without source file ./malloc/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./elf/../include/rtld-malloc.h.
    Download failed: Invalid argument.  Continuing without source file ./malloc/./malloc/malloc.c.
    Breakpoint 3 at 0x7ffff7cad670: malloc. (47 locations)
    (gdb) continue
    Continuing.
    Download failed: Invalid argument.  Continuing without source file ./malloc/./malloc/malloc.c.

    Breakpoint 3.1, __GI___libc_malloc (bytes=4) at ./malloc/malloc.c:3294
    warning: 3294   ./malloc/malloc.c: No such file or directory
    (gdb) bt
    #0  __GI___libc_malloc (bytes=4) at ./malloc/malloc.c:3294
    #1  0x000055555555534f in loop_no_free () at memory.c:38
    #2  0x0000555555555769 in main (argc=3, argv=0x7fffffffd908) at memory.c:124
 - Valgrind Result
    ==44620== LEAK SUMMARY:
    ==44620==    definitely lost: 4,000 bytes in 1,000 blocks
    ==44620==    indirectly lost: 0 bytes in 0 blocks
    ==44620==      possibly lost: 0 bytes in 0 blocks
    ==44620==    still reachable: 0 bytes in 0 blocks
    ==44620==         suppressed: 0 bytes in 0 blocks
 - Prevention: When creating lots of data in a loop, make sure that there is references to the data outside of the loop, before leaving the loop.

5: Function Scope Leave
 - GDB Analysis
    Breakpoint 1, function_scope_leave () at memory.c:45
    45          void *ptr = malloc(10);
    (gdb) next
    47      }
    (gdb) print ptr
    $1 = (void *) 0x5555555592a0
    (gdb) finish
    Run till exit from #0  function_scope_leave () at memory.c:47
    main (argc=3, argv=0x7fffffffd908) at memory.c:125
    125             case 5: function_scope_leave(); break;
 - Valgrind Result
    ==44674== LEAK SUMMARY:
    ==44674==    definitely lost: 10 bytes in 1 blocks
    ==44674==    indirectly lost: 0 bytes in 0 blocks
    ==44674==      possibly lost: 0 bytes in 0 blocks
    ==44674==    still reachable: 0 bytes in 0 blocks
    ==44674==         suppressed: 0 bytes in 0 blocks
 - Prevention: Remember to either leave with with pointer reference or free it before leaving the function scope

6: Dont Free All Data
 - GDB Analysis
    Breakpoint 1, dont_free_all_data () at memory.c:64
    64          struct LLNode *head = (struct LLNode *)malloc(sizeof(struct LLNode));
    (gdb) next
    65          if (!head) return;
    (gdb) print head
    $1 = (struct LLNode *) 0x5555555592a0
    (gdb) nex
    Ambiguous command "nex": next, nexti.
    (gdb) next
    67          head->data = 0;
    (gdb) next
    68          head->prev = NULL;
    (gdb) finish
    Run till exit from #0  dont_free_all_data () at memory.c:68
    main (argc=3, argv=0x7fffffffd908) at memory.c:126
    126             case 6: dont_free_all_data(); break;
 - Valgrind Result
    ==44736== LEAK SUMMARY:
    ==44736==    definitely lost: 24 bytes in 1 blocks
    ==44736==    indirectly lost: 0 bytes in 0 blocks
    ==44736==      possibly lost: 0 bytes in 0 blocks
    ==44736==    still reachable: 0 bytes in 0 blocks
    ==44736==         suppressed: 0 bytes in 0 blocks
 - Prevention: When working with a linked list of pointers, make sure to free all elements of the list including the head and/or the tail (depending on the list)

7: corruption
 - GDB Analysis
    Breakpoint 1, corruption () at memory.c:89
    89      static void corruption(void) {
    (gdb) next
    91          strcpy(buffer, "This string is definitely longer than 10 bytes!");
    (gdb) next
    92          printf("Buffer contents: %s\n", buffer);
    (gdb) print &buffer
    $1 = (char (*)[10]) 0x7fffffffd79e
    (gdb) next
    Buffer contents: This string is definitely longer than 10 bytes!
    93      }
    (gdb) print &buffer
    $2 = (char (*)[10]) 0x7fffffffd79e
    (gdb) next
    *** stack smashing detected ***: terminated

    Program received signal SIGABRT, Aborted.
    Download failed: Invalid argument.  Continuing without source file ./nptl/./nptl/pthread_kill.c.
    __pthread_kill_implementation (no_tid=0, signo=6, threadid=<optimized out>) at ./nptl/pthread_kill.c:44
    warning: 44     ./nptl/pthread_kill.c: No such file or directory
 - Valgrind Result
    ==44773== LEAK SUMMARY:
    ==44773==    definitely lost: 0 bytes in 0 blocks
    ==44773==    indirectly lost: 0 bytes in 0 blocks
    ==44773==      possibly lost: 0 bytes in 0 blocks
    ==44773==    still reachable: 1,024 bytes in 1 blocks
    ==44773==         suppressed: 0 bytes in 0 blocks
 - Prevention: When copying into an pointer array, ensure that the size that we are writing into it is at or below the total size of the array limit.

<Marks : 18>

Conclusion
This assignment demonstrated how memory leaks and buffer overflows occur in C programs. GDB provided a way to perform inspection of program execution and stepping through it, while Valgrind provided automated detection of leaks and invalid memory access.
 - Memory leaks occur when allocated memory is not freed
 - Pointer reassignment can silently cause leaks
 - Buffer overflows corrupt stack memory and may crash programs
 - Embedded systems are particularly vulnerable due to limited memory size 

Recommendations:
 - Always have a free() when done using data which space was made with malloc()
 - Avoid unsafe functions like strcpy that do not guarantee writing within memory bounds 
 - Use debugging tools regularly when writing code and before production deployment
 - Enable compiler warnings to warn before executing faulty code
 - Use static analysis tools
 - Proper memory management is essential in reliable embedded system development

<Marks : 7>

<Total Marks : xx/45      This will be converted to be the weighted assignment mark expressed as points. Assignments are worth 20% of the course mark, each worth 4%, expressed as points> 

 