// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H

#include "a-curl-library/curl_event_request.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gcloud_v1_embeddings_complete_cb)(
    void *arg,
    curl_event_request_t *request,
    bool   success,
    float **embeddings,         /* NULL on failure */
    size_t  num_embeddings,
    size_t  embedding_size      /* 0 if unknown/failure */
);

/* Optional stats (predictions[*].embeddings.statistics) */
typedef struct {
  int  token_count;
  int  truncated; /* 0/1 */
} gcloud_v1_embed_stats_t;

typedef void (*gcloud_v1_embeddings_complete_with_stats_cb)(
    void *arg,
    curl_event_request_t *request,
    bool   success,
    float **embeddings,
    size_t  num_embeddings,
    size_t  embedding_size,
    const gcloud_v1_embed_stats_t *stats /* array len == num_embeddings (NULL on failure) */
);

/* Basic sink (values only) */
curl_sink_interface_t *
gcloud_v1_embeddings_sink(curl_event_request_t *req,
                          size_t expected_dim,
                          gcloud_v1_embeddings_complete_cb cb,
                          void *cb_arg);

/* Sink with per-instance statistics */
curl_sink_interface_t *
gcloud_v1_embeddings_sink_with_stats(curl_event_request_t *req,
                                     size_t expected_dim,
                                     gcloud_v1_embeddings_complete_with_stats_cb cb,
                                     void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H */
