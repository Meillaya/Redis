#ifndef RDB_H
#define RDB_H

typedef struct {
    int type;
    char **elements;
    size_t length;
} ListData;

typedef struct {
    int type;
    char **elements;
    size_t length;
} SetData;

typedef struct {
    char *member;
    double score;
} ZSetElement;

typedef struct {
    int type;
    ZSetElement *elements;
    size_t length;
} ZSetData;

typedef struct {
    char *key;
    char *value;
} HashEntry;

typedef struct {
    int type;
    HashEntry *entries;
    size_t length;
} HashData;

#define TYPE_STRING 0
#define TYPE_LIST   1
#define TYPE_SET    2
#define TYPE_ZSET   3
#define TYPE_HASH   4
int load_rdb(void);

#endif 