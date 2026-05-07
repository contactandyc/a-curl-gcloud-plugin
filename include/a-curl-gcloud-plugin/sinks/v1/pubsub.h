// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_H

#include "a-curl-library/curl_event_request.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------- publish: returns messageIds[] ------- */

typedef void (*gcloud_v1_pubsub_publish_cb)(
  void *arg,
  curl_event_request_t *request,
  bool success,
  const char **message_ids, /* NULL on failure */
  size_t num_ids
);

curl_sink_interface_t *
gcloud_v1_pubsub_publish_sink(curl_event_request_t *req,
                              gcloud_v1_pubsub_publish_cb cb,
                              void *cb_arg);

/* ------- pull: returns received messages ------- */

typedef struct {
  const char  *ack_id;          /* present on envelope */
  const char  *message_id;
  const char  *publish_time;    /* RFC3339 */
  const char  *ordering_key;    /* nullable */
  const char **attr_keys;       /* parallel arrays (size == num_attributes) */
  const char **attr_vals;
  size_t       num_attributes;
  const uint8_t *data;          /* decoded bytes (pool-owned) */
  size_t       data_len;
  const char  *data_b64;        /* convenience: original base64 */
  int          delivery_attempt;/* 0 if absent */
} gcloud_v1_pubsub_received_message_t;

typedef void (*gcloud_v1_pubsub_pull_cb)(
  void *arg,
  curl_event_request_t *request,
  bool success,
  const gcloud_v1_pubsub_received_message_t *messages, /* array (pool-owned) */
  size_t num_messages
);

curl_sink_interface_t *
gcloud_v1_pubsub_pull_sink(curl_event_request_t *req,
                           gcloud_v1_pubsub_pull_cb cb,
                           void *cb_arg);

/* ------- list topics/subscriptions ------- */

typedef void (*gcloud_v1_pubsub_list_cb)(
  void *arg,
  curl_event_request_t *request,
  bool success,
  const char **names, /* array of resource names, pool-owned */
  size_t num_names,
  const char *next_page_token /* nullable */
);

curl_sink_interface_t *
gcloud_v1_pubsub_topics_list_sink(curl_event_request_t *req,
                                  gcloud_v1_pubsub_list_cb cb, void *cb_arg);

curl_sink_interface_t *
gcloud_v1_pubsub_subscriptions_list_sink(curl_event_request_t *req,
                                         gcloud_v1_pubsub_list_cb cb, void *cb_arg);

/* ------- get/create/delete: simple name extractor (or success bool) ------- */

typedef void (*gcloud_v1_pubsub_name_cb)(
  void *arg,
  curl_event_request_t *request,
  bool success,
  const char *resource_name /* nullable on failure */
);

curl_sink_interface_t *
gcloud_v1_pubsub_name_sink(curl_event_request_t *req,
                           gcloud_v1_pubsub_name_cb cb, void *cb_arg);

/* ------- empty-result sink (ack, modifyAckDeadline, delete) ------- */

typedef void (*gcloud_v1_pubsub_empty_cb)(
  void *arg,
  curl_event_request_t *request,
  bool success
);

curl_sink_interface_t *
gcloud_v1_pubsub_empty_sink(curl_event_request_t *req,
                            gcloud_v1_pubsub_empty_cb cb, void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif /* A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_H */
