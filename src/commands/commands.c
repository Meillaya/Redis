#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "commands.h"
#include "../utils/memory.h"
#include "../utils/time_utils.h"
#include "../config/config.h"
#include "../persistence/rdb.h"

KeyValue keyValueStore[MAX_KEYS];
int keyValueCount = 0;

void init_store() {
    keyValueCount = 0;
    memset(keyValueStore, 0, sizeof(keyValueStore));
    load_rdb(); // Load keys from RDB file
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


Response handle_keys(int argc, char** argv) {
    Response res;

    if (argc != 2) {
        res.response = "-ERR wrong number of arguments for 'KEYS' command\r\n";
        res.should_free = 0;
        return res;
    }

    const char* pattern = argv[1];

    // For this stage, we only handle the '*' pattern to return all keys
    if (strcmp(pattern, "*") != 0) {
        res.response = "-ERR unsupported pattern\r\n";
        res.should_free = 0;
        return res;
    }

    // Calculate the total number of matching keys
    int matched_keys = keyValueCount;

    // Calculate the size needed for the RESP array
    // RESP array starts with *<number_of_elements>\r\n
    // Each key is a bulk string: $<length>\r\n<key>\r\n
    // Estimate the buffer size. For simplicity, allocate a buffer large enough.
    // In a production environment, you'd calculate the exact size or use dynamic buffers.

    // Start with the array header
    // Assuming maximum 100 keys for buffer size estimation
    int buffer_size = 4 + (matched_keys * (10 + 1024)); // Adjust as needed
    char* buffer = safe_malloc(buffer_size);
    int offset = 0;

    // Write the RESP array header
    offset += snprintf(buffer + offset, buffer_size - offset, "*%d\r\n", matched_keys);

    // Append each key as a bulk string
    for (int i = 0; i < matched_keys; i++) {
        const char* key = keyValueStore[i].key;
        int key_length = strlen(key);
        offset += snprintf(buffer + offset, buffer_size - offset, "$%d\r\n%s\r\n", key_length, key);
    }

    res.response = buffer;
    res.should_free = 1; // The buffer was dynamically allocated and should be freed
    return res;
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
 * Handles the CONFIG GET command.
 * Syntax: CONFIG GET parameter
 * Response: RESP array containing parameter name and its value
 */
Response handle_config_get(int argc, char** argv) {
    Response res;

    if (argc != 3) {
        res.response = "-ERR wrong number of arguments for 'CONFIG GET' command\r\n";
        res.should_free = 0;
        return res;
    }

    const char* parameter = argv[2];
    char* value = NULL;

    if (strcasecmp(parameter, "dir") == 0) {
        value = config_dir;
    } else if (strcasecmp(parameter, "dbfilename") == 0) {
        value = config_dbfilename;
    } else {
        res.response = "-ERR unknown configuration parameter\r\n";
        res.should_free = 0;
        return res;
    }

    int key_len = strlen(parameter);
    int value_len = strlen(value);

    // Calculate the total response size
    int response_size = 1024; // Allocate a sufficiently large buffer

    char* buffer = safe_malloc(response_size);

    // Build the RESP array
    int written = snprintf(buffer, response_size,
        "*2\r\n"
        "$%d\r\n%s\r\n"
        "$%d\r\n%s\r\n",
        key_len, parameter,
        value_len, value);

    if (written >= response_size) {
        // Buffer wasn't large enough, reallocate and try again
        response_size = written + 1;
        buffer = realloc(buffer, response_size);
        snprintf(buffer, response_size,
            "*2\r\n"
            "$%d\r\n%s\r\n"
            "$%d\r\n%s\r\n",
            key_len, parameter,
            value_len, value);
    }

    res.response = buffer;
    res.should_free = 1;
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