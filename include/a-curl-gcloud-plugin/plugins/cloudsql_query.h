// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_CLOUDSQL_QUERY_H
#define A_CURL_GCLOUD_PLUGIN_CLOUDSQL_QUERY_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* instance_connection_name: e.g. "my-project:us-central1:my-instance" */
bool curl_event_plugin_cloudsql_query_init(
    curl_event_loop_t *loop,
    const char *instance_connection_name,
    const char *database,
    curl_event_res_id  token_id,                 /* <-- resource id from gcloud_token */
    const char *query,
    curl_sink_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // A_CURL_GCLOUD_PLUGIN_CLOUDSQL_QUERY_H
