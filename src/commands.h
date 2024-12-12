#ifndef COMMANDS_H
#define COMMANDS_H

#include "response.h"

typedef struct KeyValue {
    char* key;
    char* value;
    long long expiry;
} KeyValue;

#define MAX_KEYS 1024
#define MAX_ARGUMENT_LENGTH 256

extern KeyValue keyValueStore[MAX_KEYS];
extern int keyValueCount;

// Function declarations
Response handle_ping();
Response handle_echo(const char* argument);
Response handle_set(int argc, char** argv);
Response handle_get(const char* key);
Response handle_config_get(int argc, char** argv);
Response handle_keys(int argc, char** argv);

int find_key(const char* key);
char* get_key(const char* key);

void set_key_with_expiry(const char* key, const char* value, long long expiry);
void set_key(const char* key, const char* value);
void init_store();

#endif
