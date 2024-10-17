// src/protocol/resp.c
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "resp.h"

#define RESP_ARRAY_MAX_SIZE 1024
#define RESP_ARRAY_MAX_ELEMENT_SIZE 1024
#define MAX_ARGS 10

typedef enum {
    RESP_ARRAY,
    RESP_BULK_STRING,
    RESP_INTEGER,
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_NULL
} RESP_TYPE;

typedef struct {
    RESP_TYPE type;
    union {
        int integer;
        char* string;
        char* error;
        char* bulk_string;
        char* array[MAX_ARGS];
    };
} RESP_VALUE;

char* parse_resp(const char* input, int* argc, char** argv) {
    *argc = 0;
    const char* ptr = input;
    
    // Ensure the input starts with '*', indicating an array
    if (*ptr != '*') {
        return NULL;
    }
    ptr++;

    // Parse the number of elements in the array
    int num_elements = 0;
    while (*ptr != '\r' && *ptr != '\n' && *ptr != '\0') {
        if (!isdigit(*ptr)) {
            return NULL;
        }
        num_elements = num_elements * 10 + (*ptr - '0');
        ptr++;
    }

    // Move past CRLF
    if (*ptr == '\r' && *(ptr + 1) == '\n') {
        ptr += 2;
    } else {
        return NULL; // Invalid RESP format
    }

    for (int i = 0; i < num_elements && i < MAX_ARGS; i++) {
        if (*ptr == '$') { // Corrected from '``' to '$'
            ptr++;

            // Parse the length of the bulk string
            int len = 0;
            while (*ptr != '\r' && *ptr != '\n' && *ptr != '\0') {
                if (!isdigit(*ptr)) {
                    return NULL;
                }
                len = len * 10 + (*ptr - '0');
                ptr++;
            }

            // Move past CRLF
            if (*ptr == '\r' && *(ptr + 1) == '\n') {
                ptr += 2;
            } else {
                return NULL; // Invalid RESP format
            }

            // Allocate memory for the argument
            argv[i] = (char*)malloc(len + 1);
            if (argv[i] == NULL) {
                // Handle memory allocation failure
                // Free previously allocated memory
                for (int j = 0; j < i; j++) {
                    free(argv[j]);
                }
                return NULL;
            }

            // Copy the bulk string
            strncpy(argv[i], ptr, len);
            argv[i][len] = '\0';
            *argc += 1;

            // Move past the bulk string and CRLF
            ptr += len;
            if (*ptr == '\r' && *(ptr + 1) == '\n') {
                ptr += 2;
            } else {
                return NULL; // Invalid RESP format
            }
        } else {
            // Handle other RESP types if necessary
            // For now, we focus on bulk strings
            return NULL;
        }
    }

    return NULL; // We are not using RESP_VALUE in this context
}
