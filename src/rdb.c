#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "commands.h"
#include "memory.h"
#include "time_utils.h"
#include "config.h"
#include <ctype.h>
#include <arpa/inet.h>
/**
 * Load a single key from the RDB file and add it to the keyValueStore.
 * If the RDB file does not exist or is invalid, the store remains empty.
 * 
 * Returns 0 on success, -1 on failure.
 */

uint64_t ntohu64(uint64_t value) {
    // Convert 64-bit integer from network byte order to host byte order
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    #else
        return value;
    #endif
}

static RdbStatus rdbLoadLenEx(FILE *file, uint64_t *lenptr) {
    unsigned char buf[2];
    int type;

    if (fread(buf, 1, 1, file) != 1) {
        return RDB_STATUS_ERROR; // or handle EOF as needed
    }

    type = (buf[0] & 0xC0) >> 6;
    if (type == RDB_ENCVAL) {
        *lenptr = buf[0] & 0x3F;
    } else if (type == RDB_6BITLEN) {
        *lenptr = buf[0] & 0x3F;
    } else if (type == RDB_14BITLEN) {
        if (fread(buf + 1, 1, 1, file) != 1) {
            return RDB_STATUS_ERROR;
        }
        *lenptr = ((buf[0] & 0x3F) << 8) | buf[1];
    } else if (buf[0] == RDB_32BITLEN) {
        uint32_t len32;
        if (fread(&len32, sizeof(uint32_t), 1, file) != 1) {
            return RDB_STATUS_ERROR;
        }
        *lenptr = ntohl(len32);
    } else if (buf[0] == RDB_64BITLEN) {
        uint64_t len64;
        if (fread(&len64, sizeof(uint64_t), 1, file) != 1) {
            return RDB_STATUS_ERROR;
        }
        *lenptr = ntohu64(len64);
    } else {
       return RDB_STATUS_ERROR; // Invalid length encoding
    }

    return RDB_STATUS_OK;
}


// Function to load an RDB encoded string, using rdbLoadLenEx
static RdbStatus rdbLoadStringEx(FILE *file, char **strptr) {
    uint64_t len;
    RdbStatus status = rdbLoadLenEx(file, &len);

    if (status != RDB_STATUS_OK) {
        return status;  // Propagate error from length reading
    }

    if(len > MAX_KEY_SIZE) {
        return RDB_STATUS_ERROR;
    }


    *strptr = safe_malloc(len + 1);
    if (!*strptr) {
        return RDB_STATUS_ERROR; // Memory allocation failed
    }

    if (fread(*strptr, 1, len, file) != len) {
        free(*strptr);
        *strptr = NULL;
        return RDB_STATUS_ERROR; // Reading string failed
    }
    (*strptr)[len] = '\0';
    printf("DEBUG: String read: %s\n", *strptr);
    return RDB_STATUS_OK;
}

int load_rdb() {
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", config_dir, config_dbfilename);

    printf("DEBUG: Starting RDB load\n");
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        // RDB file does not exist; treat the database as empty
        printf("RDB file '%s' does not exist. Starting with an empty database.\n", filepath);
        return 0;
    }

    // Read Header
    char header[9];
    size_t bytes_read = fread(header, 1, 9, file);
    if (bytes_read != 9) {
        printf("Failed to read RDB header.\n");
        fclose(file);
        return -1;
    }
    printf("DEBUG: RDB Header read: %.9s\n", header);
    if (memcmp(header, "REDIS", 5) != 0 || !isdigit(header[5]) || !isdigit(header[6]) || 
        !isdigit(header[7]) || !isdigit(header[8])) {
        printf("Invalid RDB header format\n");
        fclose(file);
        return -1;
    }
    
    // Read Metadata and Database Sections
    int eof = 0;
    while (!eof && keyValueCount < MAX_KEYS) {
        int opcode = fgetc(file);
        if (opcode == EOF) {
            printf("Unexpected end of file.\n");
            break;
        }

        switch (opcode) {
            case 0xFA: { 
                // Metadata Subsection
                // Read metadata length and value
                // Metadata name
                // Since metadata name and value are string encoded, parse them
                // Read metadata name
                // For simplicity, skip metadata
                // Read metadata name
                // Parse string encoded
                // Size-encoded string
                // You can implement proper metadata parsing if needed
                // Here, we skip metadata for this challenge
                // Read metadata name
                // Function to read size-encoded string
                // Implement size encoding parsing
                // For brevity, skip bytes until next opcode
                // This is a simplification
                // In a complete implementation, you should parse the metadata properly
                // Here, we'll skip until next opcode
                // Alternatively, you can implement full parsing
                // For this challenge, metadata can be skipped
                // To skip, you might need to read and ignore
                // But skipping is not straightforward, so implement minimal parsing

                // Parse metadata name
                // Read size encoded
                // Implement a helper function to read size encoded
                // Similarly for string encoding
                // For brevity, assuming fixed length as per example
                // Read metadata name length
                int first_byte = fgetc(file);
                if (first_byte == EOF) { fclose(file); return -1; }
                int metadata_name_len;
                if ((first_byte & 0xC0) == 0x00) {
                    metadata_name_len = first_byte & 0x3F;
                } else if ((first_byte & 0xC0) == 0x40) {
                    int second_byte = fgetc(file);
                    if (second_byte == EOF) { fclose(file); return -1; }
                    metadata_name_len = ((first_byte & 0x3F) << 8) | second_byte;
                } else {
                    // Unsupported encoding
                    printf("Unsupported metadata name encoding.\n");
                    fclose(file);
                    return -1;
                }

                char *metadata_name = safe_malloc(metadata_name_len + 1);
                bytes_read = fread(metadata_name, 1, metadata_name_len, file);
                if (bytes_read != (size_t)metadata_name_len) {
                    printf("Failed to read metadata name.\n");
                    free(metadata_name);
                    fclose(file);
                    return -1;
                }
                metadata_name[metadata_name_len] = '\0';
                printf("DEBUG: Processing metadata: %s\n", metadata_name);
                // Read metadata value
                int first_byte_val = fgetc(file);
                if (first_byte_val == EOF) { free(metadata_name); fclose(file); return -1; }
                int metadata_value_len;
                // In the metadata section handling (case 0xFA)
                if ((first_byte_val & 0xC0) == 0xC0) {
                    // Integer encoding
                    switch (first_byte_val) {
                        case 0xC0: {  // 8-bit integer
                            unsigned char byte;
                            bytes_read = fread(&byte, 1, 1, file);
                            if (bytes_read != 1) {
                                free(metadata_name);
                                fclose(file);
                                return -1;
                            }
                            // Skip reading the integer value for this challenge
                            // Just continue to next section
                            free(metadata_name);
                            continue;  // Skip to next metadata entry
                        }
                        default:
                            free(metadata_name);
                            fclose(file);
                            return -1;
                    }
                }
                else if ((first_byte_val & 0xC0) == 0x00) {
                    metadata_value_len = first_byte_val & 0x3F;
                } else if ((first_byte_val & 0xC0) == 0x40) {
                    int second_byte_val = fgetc(file);
                    if (second_byte_val == EOF) { free(metadata_name); fclose(file); return -1; }
                    metadata_value_len = ((first_byte_val & 0x3F) << 8) | second_byte_val;
                } else if (first_byte_val == 0x80) {
                    // 32-bit length
                    unsigned char size_bytes[4];
                    bytes_read = fread(size_bytes, 1, 4, file);
                    if (bytes_read != 4) {
                        free(metadata_name);
                        fclose(file);
                        return -1;
                    }
                    metadata_value_len = (size_bytes[0] << 24) | (size_bytes[1] << 16) |
                                        (size_bytes[2] << 8) | size_bytes[3];
                } else if (first_byte_val == 0x81) {
                    // 64-bit length
                    unsigned char size_bytes[8];
                    bytes_read = fread(size_bytes, 1, 8, file);
                    if (bytes_read != 8) {
                        free(metadata_name);
                        fclose(file);
                        return -1;
                    }
                    metadata_value_len = ((uint64_t)size_bytes[0] << 56) | ((uint64_t)size_bytes[1] << 48) |
                                        ((uint64_t)size_bytes[2] << 40) | ((uint64_t)size_bytes[3] << 32) |
                                        ((uint64_t)size_bytes[4] << 24) | ((uint64_t)size_bytes[5] << 16) |
                                        ((uint64_t)size_bytes[6] << 8) | (uint64_t)size_bytes[7];
                }
                else {
                    // Unsupported encoding
                    printf("Unsupported metadata value encoding.\n");
                    free(metadata_name);
                    fclose(file);
                    return -1;
                }

                char *metadata_value = safe_malloc(metadata_value_len + 1);
                bytes_read = fread(metadata_value, 1, metadata_value_len, file);
                if (bytes_read != (size_t)metadata_value_len) {
                    printf("Failed to read metadata value.\n");
                    free(metadata_name);
                    free(metadata_value);
                    fclose(file);
                    return -1;
                }
                metadata_value[metadata_value_len] = '\0';

                // For this challenge, we can ignore metadata
                free(metadata_name);
                free(metadata_value);
                break;
            }
            case 0xFE: { // Database Subsection
                // Read database index
                printf("DEBUG: Processing database section\n");
                int size_opcode = fgetc(file);
                if (size_opcode == EOF) { fclose(file); return -1; }

                int db_index = 0; // Currently unused
                if ((size_opcode & 0xC0) == 0x00) {
                    db_index = size_opcode & 0x3F;
                } else if ((size_opcode & 0xC0) == 0x40) {
                    int second_byte = fgetc(file);
                    if (second_byte == EOF) { fclose(file); return -1; }
                    db_index = ((size_opcode & 0x3F) << 8) | second_byte;
                } else {
                    // Unsupported encoding
                    printf("Unsupported database index encoding.\n");
                    fclose(file);
                    return -1;
                }
                // db_index is set but not used in this implementation

                // Read hash table size for keys and expires

                int ht_size_opcode = fgetc(file);
                if (ht_size_opcode == EOF) { fclose(file); return -1; }

                printf("DEBUG: Hash table size opcode: 0x%02X\n", ht_size_opcode);

                size_t ht_size;
                if (ht_size_opcode == 0xFB) {  // Add this case
                    // Read the next byte for actual size
                    int size_byte = fgetc(file);
                    if (size_byte == EOF) { fclose(file); return -1; }
                    ht_size = (unsigned char)size_byte;
                } else if ((ht_size_opcode & 0xC0) == 0x00) {
                    ht_size = ht_size_opcode & 0x3F;
                } else if ((ht_size_opcode & 0xC0) == 0x40) {
                    int second_byte = fgetc(file);
                    if (second_byte == EOF) { fclose(file); return -1; }
                    ht_size = ((ht_size_opcode & 0x3F) << 8) | second_byte;
                } else {
                    printf("Unsupported hash table size encoding: 0x%02X\n", ht_size_opcode);
                    fclose(file);
                    return -1;
                }
                printf("DEBUG: Hash table size: %zu\n", ht_size);
                // Expires hash table size
                int expires_size_opcode = fgetc(file);
                if (expires_size_opcode == EOF) { fclose(file); return -1; }

                size_t expires_size = 0;
                if ((expires_size_opcode & 0xC0) == 0x00) {
                    expires_size = expires_size_opcode & 0x3F;
                } else if ((expires_size_opcode & 0xC0) == 0x40) {
                    int second_byte = fgetc(file);
                    if (second_byte == EOF) { fclose(file); return -1; }
                    expires_size = ((expires_size_opcode & 0x3F) << 8) | second_byte;
                } else if ((expires_size_opcode & 0xC0) == 0x80) {
                    unsigned char size_bytes[4];
                    bytes_read = fread(size_bytes, 1, 4, file);
                    if (bytes_read != 4) {
                        printf("Failed to read 4-byte expires hash table size.\n");
                        fclose(file);
                        return -1;
                    }
                    expires_size = (size_bytes[0] << 24) | (size_bytes[1] << 16) |
                                   (size_bytes[2] << 8) | size_bytes[3];
                } else {
                    // Unsupported encoding
                    printf("Unsupported expires hash table size encoding.\n");
                    fclose(file);
                    return -1;
                }

                // Read key-value pairs
                for (size_t i = 0; i < ht_size; i++) {
                    // Optional expire information
                    int peek = fgetc(file);
                    if (peek == EOF) { fclose(file); return -1; }

                    unsigned long long expire = 0;
                    if (peek == 0xFC || peek == 0xFD) {
                        // Key has an expire
                        // Seek back to read the opcode
                        if (fseek(file, -1, SEEK_CUR) != 0) {
                            fclose(file);
                            return -1;
                        }
                        unsigned char expire_type = fgetc(file);
                        if (expire_type == 0xFC) {
                            // Milliseconds
                            unsigned char expire_bytes[8];
                            bytes_read = fread(expire_bytes, 1, 8, file);
                            if (bytes_read != 8) {
                                printf("Failed to read 8-byte expire timestamp.\n");
                                fclose(file);
                                return -1;
                            }
                            // Little-endian
                            for (int b = 7; b >= 0; b--) {
                                expire = (expire << 8) | expire_bytes[b];
                            }
                        } else if (expire_type == 0xFD) {
                            // Seconds
                            unsigned char expire_bytes[4];
                            bytes_read = fread(expire_bytes, 1, 4, file);
                            if (bytes_read != 4) {
                                printf("Failed to read 4-byte expire timestamp.\n");
                                fclose(file);
                                return -1;
                            }
                            // Little-endian
                            for (int b = 3; b >= 0; b--) {
                                expire = (expire << 8) | expire_bytes[b];
                            }
                            expire *= 1000; // Convert to milliseconds
                        } else {
                            printf("Unknown expire type: 0x%02X\n", expire_type);
                            fclose(file);
                            return -1;
                        }
                    }
                    
                    // Read value type
                    int value_type = fgetc(file);
                    if (value_type == EOF) { fclose(file); return -1; }
                    printf("DEBUG: Value type: 0x%02X\n", value_type);

                    // if (value_type != 0x00 && value_type != 0x05 && value_type != 0x09 && value_type != 0x06) { 
                    //     printf("Unsupported value type: 0x%02X\n", value_type);
                    //     fclose(file);
                    //     return -1;
                    // }

                    // Read key (string encoded)
                    int key_size_opcode = fgetc(file);
                    if (key_size_opcode == EOF) { fclose(file); return -1; }

                    printf("DEBUG: Reading key size opcode: 0x%02X\n", key_size_opcode);
                    printf("DEBUG: Key size opcode bits: %02X\n", key_size_opcode & 0xC0);
                    printf("DEBUG: Key size first byte: 0x%02X\n", key_size_opcode);
                    printf("DEBUG: Key size opcode (hex): 0x%02X\n", key_size_opcode);
                    printf("DEBUG: Key size opcode (binary): %d%d%d%d%d%d%d%d\n",
                        (key_size_opcode & 0x80) ? 1 : 0,
                        (key_size_opcode & 0x40) ? 1 : 0,
                        (key_size_opcode & 0x20) ? 1 : 0,
                        (key_size_opcode & 0x10) ? 1 : 0,
                        (key_size_opcode & 0x08) ? 1 : 0,
                        (key_size_opcode & 0x04) ? 1 : 0,
                        (key_size_opcode & 0x02) ? 1 : 0,
                        (key_size_opcode & 0x01) ? 1 : 0);

                    int key_len;
                    if ((key_size_opcode & 0xC0) == 0x00) {  // 6-bit length
                        key_len = key_size_opcode & 0x3F;
                        printf("DEBUG: Using 6-bit length encoding: %d\n", key_len);
                    } else if ((key_size_opcode & 0xC0) == 0x40) {  // 14-bit length
                        int second_byte = fgetc(file);
                        if (second_byte == EOF) { fclose(file); return -1; }
                        key_len = ((key_size_opcode & 0x3F) << 8) | second_byte;
                        printf("DEBUG: Using 14-bit length encoding: %d\n", key_len);
                    } else if (key_size_opcode == 0x80) {  // 32-bit length
                        uint32_t length;
                        if (fread(&length, sizeof(uint32_t), 1, file) != 1) { fclose(file); return -1; }
                        key_len = ntohl(length); // Convert from network byte order
                        printf("DEBUG: Using 32-bit length encoding: %d\n", key_len);
                    } else if (key_size_opcode == 0x81) {  // 64-bit length
                        uint64_t length;
                        if (fread(&length, sizeof(uint64_t), 1, file) != 1) { fclose(file); return -1; }
                        key_len = ntohu64(length); // Convert from network byte order
                        printf("DEBUG: Using 64-bit length encoding: %d\n", key_len);
                    } else {
                        printf("DEBUG: Unsupported key size encoding: 0x%02X\n", key_size_opcode);
                        fclose(file);
                        return -1;
                    }


                    printf("DEBUG: Key length: %d\n", key_len);
                    printf("DEBUG: Attempting to read key of length %d\n", key_len);
                    char *key = safe_malloc(key_len + 1);
                    bytes_read = fread(key, 1, key_len, file);
                    printf("DEBUG: Key bytes: ");
                    for(size_t i = 0; i < bytes_read; i++) {
                        printf("%02X ", (unsigned char)key[i]);
                    }
                    printf("\n");
                    if (bytes_read != (size_t)key_len) {
                        printf("DEBUG: Failed to read key. Expected %d bytes, got %zu\n", 
                            key_len, bytes_read);
                        free(key);
                        fclose(file);
                        return -1;
                    }
                    key[key_len] = '\0';
                    printf("DEBUG: Successfully read key: %s\n", key);

                    // Read value (string encoded)
                    int value_size_opcode = fgetc(file);
                    if (value_size_opcode == EOF) { free(key); fclose(file); return -1; }

                    int value_len;
                    if ((value_size_opcode & 0xC0) == 0x00) {
                        value_len = value_size_opcode & 0x3F;
                    } else if ((value_size_opcode & 0xC0) == 0x40) {
                        int second_byte_val = fgetc(file);
                        if (second_byte_val == EOF) { free(key); fclose(file); return -1; }
                        value_len = ((value_size_opcode & 0x3F) << 8) | second_byte_val;
                    } else if ((value_size_opcode & 0xC0) == 0x80) {
                        unsigned char size_bytes[4];
                        bytes_read = fread(size_bytes, 1, 4, file);
                        if (bytes_read != 4) {
                            printf("Failed to read 4-byte value size.\n");
                            free(key);
                            fclose(file);
                            return -1;
                        }
                        value_len = (size_bytes[0] << 24) | (size_bytes[1] << 16) |
                                    (size_bytes[2] << 8) | size_bytes[3];
                    } else {
                        // Unsupported encoding
                        printf("Unsupported value size encoding.\n");
                        free(key);
                        fclose(file);
                        return -1;
                    }

                    char *value = safe_malloc(value_len + 1);
                    bytes_read = fread(value, 1, value_len, file);
                    if (bytes_read != (size_t)value_len) {
                        printf("Failed to read value string.\n");
                        free(key);
                        free(value);
                        fclose(file);
                        return -1;
                    }
                    value[value_len] = '\0';

                    // Add to keyValueStore
                    if (keyValueCount >= MAX_KEYS) {
                        printf("Maximum number of keys reached. Cannot add more keys from RDB.\n");
                        free(key);
                        free(value);
                        fclose(file);
                        return -1;
                    }
                    printf("DEBUG: Loaded key: %s, value: %s\n", key, value);
                    keyValueStore[keyValueCount].key = key;
                    keyValueStore[keyValueCount].value = value;
                    keyValueStore[keyValueCount].expiry = expire;
                    keyValueCount++;
                }
                
                break;
            }
            case 0xFF: { // End of File
                // Read checksum (8 bytes)
                unsigned char checksum[8];
                bytes_read = fread(checksum, 1, 8, file);
                if (bytes_read != 8) {
                    printf("Failed to read checksum.\n");
                    fclose(file);
                    return -1;
                }
                eof = 1;
                break;
            }
            default:
                printf("Unknown opcode: 0x%02X. Skipping.\n", opcode);
                // For this challenge, stop parsing on unknown opcode
                eof = 1;
                break;
        }
    }

    for (int i = 0; i < keyValueCount; i++) {
    printf("DEBUG: Stored key[%d]: %s\n", i, keyValueStore[i].key);
}
    fclose(file);
    printf("Loaded %d key(s) from RDB file '%s'.\n", keyValueCount, filepath);
    return 0;
}