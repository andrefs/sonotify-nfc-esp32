#ifndef HA_CLIENT_H_
#define HA_CLIENT_H_

#include <stdbool.h>

bool ha_client_send_webhook(const char *url, const char *content_id,
                            const char *content_type);

#endif /* HA_CLIENT_H_ */