// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_EMBEDDINGS_H
#define A_CURL_GCLOUD_PLUGIN_EMBEDDINGS_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_output.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void curl_event_plugin_google_embed_set_rate(void);

curl_event_request_t *curl_event_plugin_google_embed_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *model_id,
    int output_dimensionality,
    curl_event_res_id  token_id,            /* gcloud_token resource id */
    char **input_text,                      /* array of NUL-terminated strings */
    size_t num_texts,
    curl_output_interface_t *output_interface
);

#ifdef __cplusplus
}
#endif

#endif // A_CURL_GCLOUD_PLUGIN_EMBEDDINGS_H
