// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_GCS_DOWNLOAD_H
#define A_CURL_GCLOUD_PLUGIN_GCS_DOWNLOAD_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool curl_event_plugin_gcs_download_init(
    curl_event_loop_t *loop,
    const char *bucket,
    const char *object,
    curl_event_res_id  token_id,              /* <- resource from gcloud_token */
    curl_sink_interface_t *output_interface,
    long max_download_size
);

#ifdef __cplusplus
}
#endif

#endif // A_CURL_GCLOUD_PLUGIN_GCS_DOWNLOAD_H
