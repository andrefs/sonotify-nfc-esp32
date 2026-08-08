#ifndef DISPATCH_H_
#define DISPATCH_H_

#include <stdbool.h>
#include <stddef.h>

char *dispatch_load(void);
bool dispatch_lookup(const char *json, const char *uid_hex,
                     char *out_content_id, size_t content_id_len,
                     char *out_content_type, size_t content_type_len);

#endif /* DISPATCH_H_ */