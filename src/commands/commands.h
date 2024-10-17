#ifndef COMMANDS_H
#define COMMANDS_H

#include "response.h"

Response handle_ping();
Response handle_echo(const char* argument);
Response handle_set(const char* key, const char* value);
Response handle_get(const char* key);

#endif