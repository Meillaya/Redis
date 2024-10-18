#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "commands.h"
#include "../utils/memory.h"
#include "../utils/time_utils.h"

KeyValue keyValueStore[MAX_KEYS];
int keyValueCount = 0;

void init_store() {
    keyValueCount = 0;
    memset(keyValueStore, 0, sizeof(keyValueStore));
}
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
Response handle_set(int argc, char** argv) {
    Response res;
    if (argc < 3) {
        res.response = "-ERR wrong number of arguments for 'SET' command\r\n";
        res.should_free = 0;
        return res;
    }

    const char* key = argv[1];
    const char* value = argv[2];
    long long expiry = 0; // 0 means no expiry

    // Parse optional arguments
    int i = 3;
    while (i + 1 < argc) {
        // Compare argument case-insensitively
        if (strcasecmp(argv[i], "PX") == 0) {
            // Parse the expiry time in milliseconds
            char* endptr;
            long long px = strtoll(argv[i + 1], &endptr, 10);
            if (*endptr != '\0' || px <= 0) {
                res.response = "-ERR invalid PX value\r\n";
                res.should_free = 0;
                return res;
            }
            expiry = current_time_millis() + px;
            i += 2;
        } else {
            // Unsupported option
            res.response = "-ERR syntax error\r\n";
            res.should_free = 0;
            return res;
        }
    }

    int index = find_key(key);
    if (index != -1) {
        // Key exists, update the value and expiry
        free(keyValueStore[index].value); // Free the old value
        keyValueStore[index].value = safe_malloc(strlen(value) + 1);
        strcpy(keyValueStore[index].value, value);
        keyValueStore[index].expiry = expiry;
    } else {
        // Key does not exist, add a new key-value pair
        if (keyValueCount >= MAX_KEYS) {
            res.response = "-ERR maximum number of keys reached\r\n";
            res.should_free = 0;
            return res;
        }
        keyValueStore[keyValueCount].key = safe_malloc(strlen(key) + 1);
        strcpy(keyValueStore[keyValueCount].key, key);

        keyValueStore[keyValueCount].value = safe_malloc(strlen(value) + 1);
        strcpy(keyValueStore[keyValueCount].value, value);

        keyValueStore[keyValueCount].expiry = expiry;
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
        // Key does not exist, return null bulk string
        res.response = "$-1\r\n";
        res.should_free = 0;
    } else {
        // Check if the key has an expiry and if it has expired
        if (keyValueStore[index].expiry != 0 && current_time_millis() > keyValueStore[index].expiry) {
            // Key has expired. Remove it from the store.
            free(keyValueStore[index].key);
            free(keyValueStore[index].value);
            // Shift the remaining keys
            for(int i = index; i < keyValueCount - 1; i++) {
                keyValueStore[i] = keyValueStore[i + 1];
            }
            keyValueCount--;

            // Return null bulk string
            res.response = "$-1\r\n";
            res.should_free = 0;
        } else {
            const char* value = keyValueStore[index].value;
            int len = strlen(value);
            // Calculate the required buffer size: $<len>\r\n<value>\r\n
            // Max len of integer in string is ~10 digits
            int response_size = 1 + 20 + 2 + len + 2; // Increased buffer for larger lengths
            char* buffer = safe_malloc(response_size);
            snprintf(buffer, response_size, "$%d\r\n%s\r\n", len, value);
            res.response = buffer;
            res.should_free = 1;
        }
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