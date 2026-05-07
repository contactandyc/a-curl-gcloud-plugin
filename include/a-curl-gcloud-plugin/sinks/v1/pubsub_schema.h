// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_SCHEMA_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_SCHEMA_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-gcloud-plugin/sinks/v1/pubsub.h" /* For gcloud_v1_pubsub_empty_sink */
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER = 1,
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO            = 2
} gcloud_v1_pubsub_schema_type_t;

typedef struct {
  const char *name;                  /* projects/{p}/schemas/{id}[@{rev}] */
  gcloud_v1_pubsub_schema_type_t type;
  const char *definition;            /* may be present */
  const char *revision_id;           /* may be present */
  const char *revision_create_time;  /* RFC3339, may be present */
} gcloud_v1_pubsub_schema_t;

/* Single Schema resource result (create/get/commit/rollback) */
typedef void (*gcloud_v1_pubsub_schema_cb)(
  void *arg, curl_event_request_t *request, bool success,
  const gcloud_v1_pubsub_schema_t *schema /* pool-owned, nullable on failure */
);
curl_sink_interface_t *
gcloud_v1_pubsub_schema_sink(curl_event_request_t *req,
                             gcloud_v1_pubsub_schema_cb cb, void *cb_arg);

/* List schemas (names + page token) */
typedef void (*gcloud_v1_pubsub_schemas_list_cb)(
  void *arg, curl_event_request_t *request, bool success,
  const gcloud_v1_pubsub_schema_t *schemas, size_t count,
  const char *next_page_token /* nullable */
);
curl_sink_interface_t *
gcloud_v1_pubsub_schemas_list_sink(curl_event_request_t *req,
                                   gcloud_v1_pubsub_schemas_list_cb cb, void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif
