#include "commands.h"
#include "config.h"
#include "memory.h"
#include "rdb.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RDB_TYPE_STRING 0
#define RDB_TYPE_LIST 1
#define RDB_TYPE_SET 2
#define RDB_TYPE_ZSET 3
#define RDB_TYPE_HASH 4

#define RDB_OPCODE_EXPIRETIME 0xFC
#define RDB_OPCODE_EXPIRETIME_MS 0xFD
#define RDB_OPCODE_EOF 0xFF
#define RDB_OPCODE_RESIZEDB 0xFA
#define RDB_OPCODE_AUX 0xFA
#define RDB_ENCODING_INT8 0
#define RDB_ENCODING_INT16 1
#define RDB_ENCODING_INT32 2
#define RDB_ENCODING_LZF 3
#define RDB_OPCODE_SELECTDB 0xFE
#define RDB_OPCODE_FUNCTION2 0xF7
#define RDB_OPCODE_FUNCTION_HISTORY 0xF8
#define RDB_OPCODE_MODULE_AUX 0xF9
#define RDB_OPCODE_IDLE 0xFB

// Helper function to read a length-encoded string from RDB
static char *rdbLoadString(FILE *fp) {
  unsigned char len_byte;
  if (fread(&len_byte, 1, 1, fp) != 1) {
    fprintf(stderr, "DEBUG: rdbLoadString: Failed to read length byte\n");
    return NULL;
  }
  size_t len;
  if ((len_byte & 0xC0) == 0) {
    len = len_byte & 0x3F; // Get length from lower 6 bits
    fprintf(stderr, "DEBUG: rdbLoadString: Length type 0x00, len = %zu\n", len);
  } else if ((len_byte & 0xC0) == 0x40) {
    unsigned char next_byte;
    if (fread(&next_byte, 1, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadString: Length type 0x40, failed to read next byte\n");
      return NULL;
    }
    len = ((len_byte & 0x3F) << 8) | next_byte;
    fprintf(stderr, "DEBUG: rdbLoadString: Length type 0x40, len = %zu\n", len);
  } else if (len_byte == 0x80) {
    uint32_t len32;
    if (fread(&len32, 4, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadString: Length type 0x80, failed to read len32\n");
      return NULL;
    }
    len = len32;
    fprintf(stderr, "DEBUG: rdbLoadString: Length type 0x80, len = %zu\n", len);
  } else {
    uint64_t len64;
    if (fread(&len64, 8, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadString: Length type other, failed to read len64\n");
      return NULL;
    }
    len = len64;
    fprintf(stderr, "DEBUG: rdbLoadString: Length type other, len = %zu\n", len);
  }
  char *str = safe_malloc(len + 1);
  if (!str) {
    fprintf(stderr, "DEBUG: rdbLoadString: safe_malloc failed\n");
    return NULL;
  }
  if (fread(str, 1, len, fp) != len) {
    fprintf(stderr, "DEBUG: rdbLoadString: Failed to read string data, len = %zu\n",
            len);
    free(str);
    return NULL;
  }
  str[len] = '\0';
  fprintf(stderr, "DEBUG: rdbLoadString: String loaded: %s\n", str);
  return str;
}

// Helper function to read a length-encoded integer from RDB
static int rdbLoadInteger(FILE *fp, int enctype, long long *out) {
  fprintf(stderr, "DEBUG: rdbLoadInteger: enctype = %d\n", enctype);
  if (enctype == 0x00) { // RDB_ENC_INT8
    signed char val;
    if (fread(&val, 1, 1, fp) != 1) {
      fprintf(stderr, "DEBUG: rdbLoadInteger: Failed to read INT8\n");
      return -1;
    }
    *out = val;
    fprintf(stderr, "DEBUG: rdbLoadInteger: INT8 value = %lld\n", *out);
  } else if (enctype == 0x01) { // RDB_ENC_INT16
    int16_t val;
    if (fread(&val, 2, 1, fp) != 1) {
      fprintf(stderr, "DEBUG: rdbLoadInteger: Failed to read INT16\n");
      return -1;
    }
    *out = val;
    fprintf(stderr, "DEBUG: rdbLoadInteger: INT16 value = %lld\n", *out);
  } else if (enctype == 0x02) { // RDB_ENC_INT32
    int32_t val;
    if (fread(&val, 4, 1, fp) != 1) {
      fprintf(stderr, "DEBUG: rdbLoadInteger: Failed to read INT32\n");
      return -1;
    }
    *out = val;
    fprintf(stderr, "DEBUG: rdbLoadInteger: INT32 value = %lld\n", *out);
  } else {
    fprintf(stderr, "DEBUG: rdbLoadInteger: Unknown encoding type %d\n", enctype);
    fprintf(stderr, "Error: Unknown integer encoding type %d\n", enctype);
    return -1;
  }
  return 0;
}

// Helper function to read a length encoded value.
static int rdbLoadLen(FILE *fp, int *isencoded, size_t *lenptr) {
  unsigned char byte;
  if (fread(&byte, 1, 1, fp) != 1) {
    fprintf(stderr, "DEBUG: rdbLoadLen: Failed to read length byte\n");
    return -1;
  }
  if (isencoded)
    *isencoded = 0;
  int type = (byte & 0xC0) >> 6;
  fprintf(stderr, "DEBUG: rdbLoadLen: length byte = %x, type = %d\n", byte,
          type);
  if (type == 0x03) {
    // encoded value
    if (isencoded)
      *isencoded = 1;
    *lenptr = byte & 0x3F;
    fprintf(stderr, "DEBUG: rdbLoadLen: Encoded length, len = %zu\n", *lenptr);
  } else if (type == 0x00) {
    *lenptr = byte & 0x3F;
    fprintf(stderr, "DEBUG: rdbLoadLen: Length type 0x00, len = %zu\n", *lenptr);
  } else if (type == 0x01) {
    unsigned char nextByte;
    if (fread(&nextByte, 1, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadLen: Length type 0x01, failed to read next byte\n");
      return -1;
    }
    *lenptr = ((byte & 0x3F) << 8) | nextByte;
    fprintf(stderr, "DEBUG: rdbLoadLen: Length type 0x01, len = %zu\n", *lenptr);
  } else if (byte == 0x80) {
    uint32_t len32;
    if (fread(&len32, 4, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadLen: Length type 0x80, failed to read len32\n");
      return -1;
    }
    *lenptr = len32;
    fprintf(stderr, "DEBUG: rdbLoadLen: Length type 0x80, len = %zu\n", *lenptr);
  } else if (byte == 0x81) {
    uint64_t len64;
    if (fread(&len64, 8, 1, fp) != 1) {
      fprintf(stderr,
              "DEBUG: rdbLoadLen: Length type 0x81, failed to read len64\n");
      return -1;
    }
    *lenptr = len64;
    fprintf(stderr, "DEBUG: rdbLoadLen: Length type 0x81, len = %zu\n", *lenptr);
  } else {
    fprintf(stderr, "DEBUG: rdbLoadLen: Unknown length encoding type %d\n",
            type);
    fprintf(stderr, "Error: Unknown length encoding type %d\n", type);
    return -1;
  }
  return 0;
}

// Helper function to read a double value from RDB, this implementation does not
// handle special cases
static int rdbLoadDoubleValue(FILE *fp, double *val) {
  unsigned char len;
  if (fread(&len, 1, 1, fp) != 1) {
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: Failed to read length byte\n");
    return -1;
  }
  fprintf(stderr, "DEBUG: rdbLoadDoubleValue: len byte = %x\n", len);
  if (len == 255) {
    *val = -1.0 / 0.0;
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: value = -inf\n");
    return 0;
  }
  if (len == 254) {
    *val = 1.0 / 0.0;
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: value = inf\n");
    return 0;
  }
  if (len == 253) {
    *val = 0.0 / 0.0;
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: value = nan\n");
    return 0;
  }
  char *str = safe_malloc(len + 1);
  if (fread(str, 1, len, fp) != len) {
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: Failed to read string data\n");
    free(str);
    return -1;
  }
  str[len] = '\0';
  if (sscanf(str, "%lg", val) != 1) {
    fprintf(stderr, "DEBUG: rdbLoadDoubleValue: sscanf failed\n");
    free(str);
    return -1;
  }
  fprintf(stderr, "DEBUG: rdbLoadDoubleValue: value = %f\n", *val);
  free(str);
  return 0;
}

// Helper function to read a raw value.
static int rdbLoadRaw(FILE *fp, char **out, size_t len) {
  fprintf(stderr, "DEBUG: rdbLoadRaw: len = %zu\n", len);
  *out = safe_malloc(len + 1);
  if (!*out) {
    fprintf(stderr, "DEBUG: rdbLoadRaw: safe_malloc failed\n");
    return -1;
  }
  if (fread(*out, 1, len, fp) != len) {
    fprintf(stderr, "DEBUG: rdbLoadRaw: Failed to read raw data\n");
    free(*out);
    return -1;
  }
  (*out)[len] = '\0';
  fprintf(stderr, "DEBUG: rdbLoadRaw: Raw data loaded: %s\n", *out);
  return 0;
}

static int decode_string(char *dest, unsigned char *src) {
  fprintf(stderr, "DEBUG: decode_string: src[0] = %x\n", src[0]);
  if (src[0] <= 0x0D) {
    int length = (int)src[0];
    strncpy(dest, (char *)(src + 1), length);
    dest[length] = '\0';
    fprintf(stderr,
            "DEBUG: decode_string: type 0x00-0x0d, dest = %s, bytes used = %d\n",
            dest, length + 1);
    return length + 1;
  } else if (src[0] == 0xC0) {
    sprintf(dest, "%d", src[1]);
    fprintf(stderr, "DEBUG: decode_string: type 0xc0, dest = %s, bytes used = 2\n",
            dest);
    return 2;
  } else if (src[0] == 0xC1) {
    sprintf(dest, "%d", (int)(src[2] << 8) + (int)(src[1]));
    fprintf(stderr, "DEBUG: decode_string: type 0xc1, dest = %s, bytes used = 3\n",
            dest);
    return 3;
  } else if (src[0] == 0xC2) {
    sprintf(dest, "%d",
            (int)(src[4] << 24) + (int)(src[3] << 16) + (int)(src[2] << 8) +
                (int)src[1]);
    fprintf(stderr, "DEBUG: decode_string: type 0xc2, dest = %s, bytes used = 5\n",
            dest);
    return 5;
  }
  fprintf(stderr, "DEBUG: decode_string: Unknown string encoding\n");
  return 0;
}

static int decode_size(char *dest, unsigned char *src) {
  unsigned char first_two_bits = src[0] >> 6;
  int num_of_bytes_used = 0;
  int result = 0;
  fprintf(stderr, "DEBUG: decode_size: src[0] = %x\n", src[0]);
  if (first_two_bits == 0b00) {
    result = (int)src[0];
    num_of_bytes_used = 1;
    fprintf(stderr,
            "DEBUG: decode_size: type 0b00, result = %d, bytes used = %d\n",
            result, num_of_bytes_used);
  } else if (first_two_bits == 0b01) {
    result = (int)(src[0] & 0x3F);
    result = (result << 8) + (int)src[1];
    num_of_bytes_used = 2;
    fprintf(stderr,
            "DEBUG: decode_size: type 0b01, result = %d, bytes used = %d\n",
            result, num_of_bytes_used);
  } else if (first_two_bits == 0b10) {
    result = (int)(src[1] << 24) + (int)(src[2] << 16) + (int)(src[3] << 8) +
             (int)src[4];
    num_of_bytes_used = 5;
    fprintf(stderr,
            "DEBUG: decode_size: type 0b10, result = %d, bytes used = %d\n",
            result, num_of_bytes_used);
  } else if (first_two_bits == 0b11) {
    num_of_bytes_used = decode_string(dest, src);
    fprintf(stderr, "DEBUG: decode_size: type 0b11, dest = %s, bytes used = %d\n",
            dest, num_of_bytes_used);
    return num_of_bytes_used;
  }
  sprintf(dest, "%d", result);
  fprintf(stderr, "DEBUG: decode_size: final dest = %s\n", dest);
  return num_of_bytes_used;
}

int load_rdb() {
  FILE *fp;
  unsigned char content[1024];
  char rdb_path[1024];
  snprintf(rdb_path, sizeof(rdb_path), "%s/%s", config_dir, config_dbfilename);
  fprintf(stderr, "DEBUG: load_rdb: rdb_path = %s\n", rdb_path);
  fp = fopen(rdb_path, "rb");
  if (!fp) {
    fprintf(stderr, "DEBUG: load_rdb: fopen failed\n");
    return 0;
  }
  size_t bytes_read = fread(content, sizeof(char), sizeof(content), fp);
  if (bytes_read == 0) {
    fprintf(stderr, "DEBUG: load_rdb: fread returned 0\n");
    fclose(fp);
    return -1;
  }
  fprintf(stderr, "DEBUG: load_rdb: bytes_read = %zu\n", bytes_read);
  int index = 9; // Skip REDIS0011 header
  fprintf(stderr, "DEBUG: load_rdb: index = %d, content[index] = %x\n", index,
          content[index]);

  // Process AUX fields
  while (content[index] == 0xFA) {
    fprintf(stderr, "DEBUG: load_rdb: AUX field found\n");
    index++;
    char key[100] = {0};
    char value[100] = {0};
    int key_length = decode_string(key, &content[index]);
    index += key_length;
    int value_length = decode_string(value, &content[index]);
    index += value_length;
    fprintf(stderr, "DEBUG: load_rdb: AUX key = %s, value = %s\n", key, value);
  }

  // Handle database selection
  if (content[index] == 0xFE) {
    fprintf(stderr, "DEBUG: load_rdb: SELECTDB opcode found\n");
    index++;
    char result[100];
    index += decode_size(result, &content[index]);
    fprintf(stderr, "DEBUG: load_rdb: SELECTDB result = %s\n", result);
  }

  // Handle hash table info
  if (content[index] == 0xFB) {
    fprintf(stderr, "DEBUG: load_rdb: IDLE opcode found\n");
    index++;
    char size_of_key_value_ht_string[100];
    int size_of_key_value_ht_string_length =
        decode_size(size_of_key_value_ht_string, &content[index]);
    index += size_of_key_value_ht_string_length;
    fprintf(stderr, "DEBUG: load_rdb: size_of_key_value_ht = %s\n",
            size_of_key_value_ht_string);

    char size_of_expiry_ht_string[100];
    int size_of_key_expiry_string_length =
        decode_size(size_of_expiry_ht_string, &content[index]);
    index += size_of_key_expiry_string_length;
    fprintf(stderr, "DEBUG: load_rdb: size_of_expiry_ht = %s\n",
            size_of_expiry_ht_string);
  }

  // Process key-value pairs
  while (content[index] != 0xFF) {
    fprintf(stderr, "DEBUG: load_rdb: Processing key-value pair at index %d, byte %x\n", index, content[index]);
    long long expiry_time = -1;

    // Handle expiry time opcodes
    if (content[index] == RDB_OPCODE_EXPIRETIME) {
        fprintf(stderr, "DEBUG: load_rdb: Found EXPIRETIME opcode\n");
        index++;
        uint32_t time_sec;
        if ((size_t)index + 4 > bytes_read) {
            fprintf(stderr, "DEBUG: load_rdb: Not enough bytes for EXPIRETIME\n");
            fclose(fp);
            return -1;
        }
        time_sec = (uint32_t)(content[index]) |
                  (uint32_t)(content[index + 1] << 8) |
                  (uint32_t)(content[index + 2] << 16) |
                  (uint32_t)(content[index + 3] << 24);
        expiry_time = (long long)time_sec * 1000LL;
        fprintf(stderr, "DEBUG: load_rdb: Expiry time: %lld ms\n", expiry_time);
        index += 4;
    } else if (content[index] == RDB_OPCODE_EXPIRETIME_MS) {
          fprintf(stderr, "DEBUG: load_rdb: Found EXPIRETIME_MS opcode\n");
          index++;
          uint64_t time_ms;
          if ((size_t)index + 8 > bytes_read) {
              fprintf(stderr, "DEBUG: load_rdb: Not enough bytes for EXPIRETIME_MS\n");
              fclose(fp);
              return -1;
          }
           time_ms = (uint64_t)content[index] |
                    ((uint64_t)content[index + 1] << 8) |
                    ((uint64_t)content[index + 2] << 16) |
                    ((uint64_t)content[index + 3] << 24) |
                    ((uint64_t)content[index + 4] << 32) |
                    ((uint64_t)content[index + 5] << 40) |
                    ((uint64_t)content[index + 6] << 48) |
                    ((uint64_t)content[index + 7] << 56);

          expiry_time = (long long)time_ms;
          fprintf(stderr, "DEBUG: load_rdb: Expiry time: %lld ms\n", expiry_time);
           index += 8;
      }

      // Check value type
       fprintf(stderr, "DEBUG: load_rdb: Reading value type at index %d: %x\n", index, content[index]);
    if (content[index] == 0x00) { // RDB_TYPE_STRING == 0x00

        index++;

      // Read key-value pair
        char key[100] = {0};
        char value[100] = {0};
    
        int key_length = decode_string(key, &content[index]);
        fprintf(stderr, "DEBUG: load_rdb: Key: %s (length: %d)\n", key, key_length);
        index += key_length;

        int value_length = decode_string(value, &content[index]);
        fprintf(stderr, "DEBUG: load_rdb: Value: %s (length: %d)\n", value, value_length);
        index += value_length;

         fprintf(stderr, "DEBUG: load_rdb: Setting key-value pair with expiry: %lld\n", expiry_time);
        set_key_with_expiry(key, value, expiry_time);
      } else {
           fprintf(stderr, "DEBUG: load_rdb: Invalid value type %x, should be 0x00\n", content[index]);
          fclose(fp);
           return -1;
      }
  }

  fprintf(stderr, "DEBUG: load_rdb: EOF reached\n");
  fclose(fp);
  return 0;
}