// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_H
#define A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Endpoint helpers ---------- */

static inline const char *gcloud_v1_pubsub_endpoint_global(void) {
  return "https://pubsub.googleapis.com";
}
/* Writes "https://{location}-pubsub.googleapis.com" into buf */
void gcloud_v1_pubsub_build_locational_endpoint(char *buf, size_t buflen, const char *location);
/* Writes "https://pubsub.{region}.rep.googleapis.com" into buf */
void gcloud_v1_pubsub_build_regional_endpoint(char *buf, size_t buflen, const char *region);

/* ---------- Common path helpers (URL-safe) ---------- */
/* Percent-encode a path segment. Returns pool-owned string. */
const char *gcloud_v1_pubsub_url_encode_segment(aml_pool_t *pool, const char *segment);

/* ---------- Topics ---------- */

/* PUT /v1/projects/{project}/topics/{topic} */
curl_event_request_t *
gcloud_v1_pubsub_topics_create_init(curl_event_loop_t *loop,
                                    curl_event_res_id  token_id,
                                    const char        *base_endpoint,
                                    const char        *project_id,
                                    const char        *topic_id);
/* Optional decorators on create body */
void gcloud_v1_pubsub_topics_create_set_kms_key(curl_event_request_t *req, const char *kms_key_name);
void gcloud_v1_pubsub_topics_create_add_label(curl_event_request_t *req, const char *key, const char *value);

/* GET /v1/projects/{project}/topics/{topic} */
curl_event_request_t *
gcloud_v1_pubsub_topics_get_init(curl_event_loop_t *loop,
                                 curl_event_res_id  token_id,
                                 const char        *base_endpoint,
                                 const char        *project_id,
                                 const char        *topic_id);

/* DELETE /v1/projects/{project}/topics/{topic} */
curl_event_request_t *
gcloud_v1_pubsub_topics_delete_init(curl_event_loop_t *loop,
                                    curl_event_res_id  token_id,
                                    const char        *base_endpoint,
                                    const char        *project_id,
                                    const char        *topic_id);

/* GET /v1/projects/{project}/topics?pageSize=&pageToken= */
curl_event_request_t *
gcloud_v1_pubsub_topics_list_init(curl_event_loop_t *loop,
                                  curl_event_res_id  token_id,
                                  const char        *base_endpoint,
                                  const char        *project_id);
void gcloud_v1_pubsub_topics_list_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_topics_list_set_page_token(curl_event_request_t *req, const char *page_token);

/* POST /v1/projects/{project}/topics/{topic}:publish */
curl_event_request_t *
gcloud_v1_pubsub_topics_publish_init(curl_event_loop_t *loop,
                                     curl_event_res_id  token_id,
                                     const char        *base_endpoint,
                                     const char        *project_id,
                                     const char        *topic_id);
/* Add messages to publish body; bytes will be base64-encoded for you */
void gcloud_v1_pubsub_topics_publish_add_message_bytes(curl_event_request_t *req,
                                                       const void *data, size_t len,
                                                       const char **attr_keys /*nullable*/,
                                                       const char **attr_vals /*nullable*/,
                                                       size_t n_attrs,
                                                       const char *ordering_key /*nullable*/);
/* Or add a message with pre-encoded base64 */
void gcloud_v1_pubsub_topics_publish_add_message_b64(curl_event_request_t *req,
                                                     const char *data_b64,
                                                     const char **attr_keys /*nullable*/,
                                                     const char **attr_vals /*nullable*/,
                                                     size_t n_attrs,
                                                     const char *ordering_key /*nullable*/);

/* ---------- Subscriptions ---------- */

/* PUT /v1/projects/{project}/subscriptions/{subscription} (body contains topic=projects/.../topics/...) */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_create_init(curl_event_loop_t *loop,
                                           curl_event_res_id  token_id,
                                           const char        *base_endpoint,
                                           const char        *project_id,
                                           const char        *subscription_id,
                                           const char        *topic_full_name /* "projects/.../topics/..." */);
/* Optional decorators on subscription create body */
void gcloud_v1_pubsub_subscriptions_create_set_ack_deadline(curl_event_request_t *req, int seconds);
void gcloud_v1_pubsub_subscriptions_create_enable_exactly_once(curl_event_request_t *req, bool on);
void gcloud_v1_pubsub_subscriptions_create_set_push_endpoint(curl_event_request_t *req, const char *url);
void gcloud_v1_pubsub_subscriptions_create_set_filter(curl_event_request_t *req, const char *filter);

/* GET /v1/projects/{project}/subscriptions/{subscription} */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_get_init(curl_event_loop_t *loop,
                                        curl_event_res_id  token_id,
                                        const char        *base_endpoint,
                                        const char        *project_id,
                                        const char        *subscription_id);

/* DELETE /v1/projects/{project}/subscriptions/{subscription} */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_delete_init(curl_event_loop_t *loop,
                                           curl_event_res_id  token_id,
                                           const char        *base_endpoint,
                                           const char        *project_id,
                                           const char        *subscription_id);

/* GET /v1/projects/{project}/subscriptions?pageSize=&pageToken= */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_list_init(curl_event_loop_t *loop,
                                         curl_event_res_id  token_id,
                                         const char        *base_endpoint,
                                         const char        *project_id);
void gcloud_v1_pubsub_subscriptions_list_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_subscriptions_list_set_page_token(curl_event_request_t *req, const char *page_token);

/* POST /v1/projects/{project}/subscriptions/{subscription}:pull */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_pull_init(curl_event_loop_t *loop,
                                         curl_event_res_id  token_id,
                                         const char        *base_endpoint,
                                         const char        *project_id,
                                         const char        *subscription_id);
void gcloud_v1_pubsub_subscriptions_pull_set_max_messages(curl_event_request_t *req, int max_messages);
/* returnImmediately is deprecated; we keep it as an opt-in */
void gcloud_v1_pubsub_subscriptions_pull_set_return_immediately(curl_event_request_t *req, bool on);

/* POST /v1/projects/{project}/subscriptions/{subscription}:acknowledge */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_ack_init(curl_event_loop_t *loop,
                                        curl_event_res_id  token_id,
                                        const char        *base_endpoint,
                                        const char        *project_id,
                                        const char        *subscription_id);
void gcloud_v1_pubsub_subscriptions_ack_add_id(curl_event_request_t *req, const char *ack_id);

/* POST /v1/projects/{project}/subscriptions/{subscription}:modifyAckDeadline */
curl_event_request_t *
gcloud_v1_pubsub_subscriptions_modify_ack_deadline_init(curl_event_loop_t *loop,
                                                        curl_event_res_id  token_id,
                                                        const char        *base_endpoint,
                                                        const char        *project_id,
                                                        const char        *subscription_id);
void gcloud_v1_pubsub_subscriptions_modify_ack_deadline_set_seconds(curl_event_request_t *req, int seconds);
void gcloud_v1_pubsub_subscriptions_modify_ack_deadline_add_id(curl_event_request_t *req, const char *ack_id);

/* Submit helper (matches embeddings style) */
static inline curl_event_request_t *
gcloud_v1_pubsub_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
  return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_H */
