// src/commands/commands.h
#ifndef COMMANDS_H
#define COMMANDS_H

#include "response.h"

// Define the struct with a tag name to avoid anonymity
typedef struct KeyValue {
    char* key;
    char* value;
    long long expiry; // Expiry time in milliseconds since epoch. 0 means no expiry.
} KeyValue;

#define MAX_KEYS 1000

// Extern declarations for shared variables
extern KeyValue keyValueStore[MAX_KEYS];
extern int keyValueCount;

// Function declarations
Response handle_ping();
Response handle_echo(const char* argument);
Response handle_set(int argc, char** argv); 
Response handle_get(const char* key);
Response handle_config_get(int argc, char** argv);
Response handle_keys(int argc, char** argv);
// Initialize the key-value store
void init_store();

#endif
