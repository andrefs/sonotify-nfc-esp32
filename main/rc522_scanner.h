#ifndef RC522_SCANNER_H_
#define RC522_SCANNER_H_

#include "esp_err.h"

esp_err_t rc522_scanner_init(const char *dispatch_json);

#endif /* RC522_SCANNER_H_ */