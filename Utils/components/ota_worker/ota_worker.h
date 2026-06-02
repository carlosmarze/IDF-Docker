#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char  url[160];
    char  sha256_expected[65];
    bool  reboot_after;
    bool  partial_download;
    int   chunk_size;
} ota_job_t;

bool ota_worker_start(void);
bool ota_submit(const ota_job_t *job);

#ifdef __cplusplus
}
#endif
