// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_SCHEMA_H
#define A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_SCHEMA_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_pool.h"
#include "a-json-library/ajson.h"
#include "a-curl-gcloud-plugin/plugins/v1/pubsub.h"  /* for endpoint helpers + URL-encode */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER = 1,
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO            = 2
} gcloud_v1_pubsub_schema_type_t;

typedef enum {
  GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_BINARY = 1,
  GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_JSON   = 2
} gcloud_v1_pubsub_message_encoding_t;

/* ---------- Create: POST /v1/projects/{project}/schemas?schemaId= ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_create_init(curl_event_loop_t *loop,
                                     curl_event_res_id  token_id,
                                     const char        *base_endpoint,
                                     const char        *project_id,
                                     const char        *schema_id);
/* Body setters (root is the Schema resource: {type, definition}) */
void gcloud_v1_pubsub_schemas_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_set_definition(curl_event_request_t *req, const char *definition);

/* ---------- Get/Delete/List ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_get_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                  const char *base_endpoint, const char *project_id, const char *schema_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                     const char *base_endpoint, const char *project_id, const char *schema_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                   const char *base_endpoint, const char *project_id);
void gcloud_v1_pubsub_schemas_list_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_schemas_list_set_page_token(curl_event_request_t *req, const char *page_token);

/* ---------- Revisions: commit/rollback/delete/list ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_commit_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                     const char *base_endpoint, const char *project_id, const char *schema_id);
/* Commit body has nested "schema": {type, definition} */
void gcloud_v1_pubsub_schemas_commit_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_commit_set_definition(curl_event_request_t *req, const char *definition);

curl_event_request_t *
gcloud_v1_pubsub_schemas_rollback_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                       const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_rollback_set_revision_id(curl_event_request_t *req, const char *revision_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_revision_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                              const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_delete_revision_set_revision_id(curl_event_request_t *req, const char *revision_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_revisions_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                             const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_list_revisions_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_schemas_list_revisions_set_page_token(curl_event_request_t *req, const char *page_token);

/* ---------- Validate & ValidateMessage ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                       const char *base_endpoint, const char *project_id);
/* body: { schema: { type, definition } } */
void gcloud_v1_pubsub_schemas_validate_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_validate_set_definition(curl_event_request_t *req, const char *definition);

curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_message_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                               const char *base_endpoint, const char *project_id);
/* choose schema by name OR inline */
void gcloud_v1_pubsub_schemas_validate_message_use_schema_name(curl_event_request_t *req, const char *full_name);
void gcloud_v1_pubsub_schemas_validate_message_set_inline(curl_event_request_t *req,
                                                          gcloud_v1_pubsub_schema_type_t type,
                                                          const char *definition);
/* message and encoding */
void gcloud_v1_pubsub_schemas_validate_message_set_encoding(curl_event_request_t *req,
                                                            gcloud_v1_pubsub_message_encoding_t encoding);
void gcloud_v1_pubsub_schemas_validate_message_set_bytes(curl_event_request_t *req,
                                                         const void *data, size_t len);
void gcloud_v1_pubsub_schemas_validate_message_set_json(curl_event_request_t *req,
                                                        const char *json_string);

#ifdef __cplusplus
}
#endif
#endif
