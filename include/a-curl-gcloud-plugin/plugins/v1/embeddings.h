// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H
#define A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task type string constants (from Vertex docs) */
#define GCLOUD_EMBED_TASK_RETRIEVAL_QUERY     "RETRIEVAL_QUERY"
#define GCLOUD_EMBED_TASK_RETRIEVAL_DOCUMENT  "RETRIEVAL_DOCUMENT"
#define GCLOUD_EMBED_TASK_SEMANTIC_SIMILARITY "SEMANTIC_SIMILARITY"
#define GCLOUD_EMBED_TASK_CLASSIFICATION      "CLASSIFICATION"
#define GCLOUD_EMBED_TASK_CLUSTERING          "CLUSTERING"
#define GCLOUD_EMBED_TASK_QUESTION_ANSWERING  "QUESTION_ANSWERING"
#define GCLOUD_EMBED_TASK_FACT_VERIFICATION   "FACT_VERIFICATION"
#define GCLOUD_EMBED_TASK_CODE_RETRIEVAL_QUERY "CODE_RETRIEVAL_QUERY"

/**
 * Region-aware builder:
 *   https://{region}-aiplatform.googleapis.com/v1/projects/{project}/locations/{region}/publishers/google/models/{model}:predict
 *
 * Dependencies:
 *   token_id -> gcloud_token_payload_t* (see token.h)
 */
curl_event_request_t *
gcloud_v1_embeddings_init_region(curl_event_loop_t *loop,
                                 curl_event_res_id  token_id,
                                 const char        *project_id,
                                 const char        *region,
                                 const char        *model_id);

/* Back-compat helper (defaults region to "us-central1") */
static inline curl_event_request_t *
gcloud_v1_embeddings_init(curl_event_loop_t *loop,
                          curl_event_res_id  token_id,
                          const char        *project_id,
                          const char        *model_id)
{
  return gcloud_v1_embeddings_init_region(loop, token_id, project_id, "us-central1", model_id);
}

/* Instances — simple API */
void gcloud_v1_embeddings_add_text (curl_event_request_t *req, const char *text);
void gcloud_v1_embeddings_add_texts(curl_event_request_t *req, const char **texts, size_t n);
/* Instances — full control (content + task_type + title) */
void gcloud_v1_embeddings_add_instance(curl_event_request_t *req,
                                       const char *content,
                                       const char *task_type /*nullable*/,
                                       const char *title     /*nullable*/);
/* Instances — convenience */
static inline void gcloud_v1_embeddings_add_query(curl_event_request_t *req, const char *content) {
  gcloud_v1_embeddings_add_instance(req, content, GCLOUD_EMBED_TASK_RETRIEVAL_QUERY, NULL);
}
static inline void gcloud_v1_embeddings_add_document(curl_event_request_t *req,
                                                     const char *content,
                                                     const char *title /*nullable*/) {
  gcloud_v1_embeddings_add_instance(req, content, GCLOUD_EMBED_TASK_RETRIEVAL_DOCUMENT, title);
}

/* Parameters */
void gcloud_v1_embeddings_set_output_dimensionality(curl_event_request_t *req, int dimensions);
void gcloud_v1_embeddings_set_auto_truncate       (curl_event_request_t *req, bool on);

/* Submit helper */
static inline curl_event_request_t *
gcloud_v1_embeddings_submit(curl_event_loop_t *loop,
                            curl_event_request_t *req,
                            int priority) {
  return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H */
