// src/commands/response.h
#ifndef RESPONSE_H
#define RESPONSE_H

typedef struct {
    const char* response;
    int should_free;
} Response;

#endif