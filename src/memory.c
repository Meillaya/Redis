#include <stdlib.h>
#include <stdio.h>
#include "memory.h"

/**
 * Allocates memory and checks for allocation failure.
 * If allocation fails, prints an error and exits the program.
 */
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed. Exiting.\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}