#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "commands.h"
#include <ctype.h>
#include "../utils/memory.h"

#define MAX_KEYS 1000

typedef struct {
    char* key;
    char* value;
} KeyValue;


KeyValue keyValueStore[MAX_KEYS];
int keyValueCount = 0;

/**
 * Finds the index of a key in the keyValueStore.
 * Returns the index if found, otherwise -1.
 */
int find_key(const char* key) {
    for (int i = 0; i < keyValueCount; i++) {
        if (strcmp(keyValueStore[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Handles the SET command.
 * Syntax: SET key value
 * Response: +OK\r\n
 */
Response handle_set(const char* key, const char* value) {
    Response res;
    if (key == NULL || value == NULL) {
        res.response = "-ERR wrong number of arguments for 'SET' command\r\n";
        res.should_free = 0;
        return res;
    }

    int index = find_key(key);
    if (index != -1) {
        // Update existing key
        free(keyValueStore[index].value);
        keyValueStore[index].value = safe_malloc(strlen(value) + 1);
        strcpy(keyValueStore[index].value, value);
    } else {
        // Add new key-value pair
        if (keyValueCount >= MAX_KEYS) {
            res.response = "-ERR maximum number of keys reached\r\n";
            res.should_free = 0;
            return res;
        }
        keyValueStore[keyValueCount].key = safe_malloc(strlen(key) + 1);
        strcpy(keyValueStore[keyValueCount].key, key);

        keyValueStore[keyValueCount].value = safe_malloc(strlen(value) + 1);
        strcpy(keyValueStore[keyValueCount].value, value);

        keyValueCount++;
    }

    res.response = "+OK\r\n";
    res.should_free = 0; // String literal
    return res;
}

/**
 * Handles the GET command.
 * Syntax: GET key
 * Response:
 *   - If key exists: $<length>\r\n<value>\r\n
 *   - If key does not exist: $-1\r\n
 */
Response handle_get(const char* key) {
    Response res;
    if (key == NULL) {
        res.response = "-ERR wrong number of arguments for 'GET' command\r\n";
        res.should_free = 0;
        return res;
    }

    int index = find_key(key);
    if (index == -1) {
        res.response = "$-1\r\n";
        res.should_free = 0;
    } else {
        const char* value = keyValueStore[index].value;
        int len = strlen(value);
        int response_size = 1 + 10 + 2 + len + 2;
        char* buffer = safe_malloc(response_size);
        snprintf(buffer, response_size, "$%d\r\n%s\r\n", len, value);
        res.response = buffer;
        res.should_free = 1;
    }
    return res;
}


/**
 * Handles the PING command.
 * Response: +PONG\r\n
 */
Response handle_ping() {
    Response res;
    res.response = "+PONG\r\n";
    res.should_free = 0; // String literal
    return res;
}

/**
 * Handles the ECHO command.
 * Response: $<len>\r\n<argument>\r\n
 */
Response handle_echo(const char* argument) {
    Response res;
    if (argument == NULL) {
        res.response = "-ERR wrong number of arguments for 'ECHO' command\r\n";
        res.should_free = 0;
        return res;
    }

    int len = strlen(argument);
    int response_size = 1 + 10 + 2 + len + 2;
    char* buffer = safe_malloc(response_size);
    snprintf(buffer, response_size, "$%d\r\n%s\r\n", len, argument);
    res.response = buffer;
    res.should_free = 1;
    return res;
}