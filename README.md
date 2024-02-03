# File System Construction Exercise

### 1. Objective

This exercise involves writing a program in the Minix system environment using the C language to implement basic file system functions. The file system should be organized in a large file of specified size, acting as a "virtual disk." The program is responsible for creating the virtual disk, performing reads and writes to execute fundamental disk operations related to directory management, file allocation, and maintaining name uniqueness.

### 2. Program Functions

The file system should be organized within a single-level directory in the file on the virtual disk. The directory item should include at least the file name, size, and file placement on the virtual disk. The program should implement the following user-accessible operations:

- Creating a virtual disk
- Copying a file from the Minix system disk to the virtual disk
- Copying a file from the virtual disk to the Minix system disk
- Displaying the virtual disk directory
- Deleting a file from the virtual disk
- Deleting the virtual disk
- Displaying a report with the current map of virtual disk occupancy – a list of consecutive areas on the virtual disk with details such as address, area type, size, and state (e.g., for data blocks: free/occupied).

The program should monitor the available space on the virtual disk and the directory's capacity, responding to attempts to exceed these limits.

### 3. Notes

- The program does not implement file opening, reading, or writing.
- Concurrency-related functions are not implemented; sequential and exclusive access to the virtual disk is assumed.
- The demonstration script showcases both strengths and weaknesses of the adopted solution, considering external and internal fragmentation.

### 4. Implementation

My solution is in the main.c file. Files a, b, c and d are exaples of simple text files we can copy into our file sytem. test and test2 are sh scripts to run and see the outcomes of some actions performed on the file system. The main file itself has got "interactive" mode so the user can feel free to test out the FS on his own.
