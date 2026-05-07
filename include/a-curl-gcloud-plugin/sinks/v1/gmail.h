// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_GMAIL_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_GMAIL_H

#include "a-curl-library/curl_event_request.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------- Contact Parser Struct ------- */
typedef struct {
    const char *name;  /* e.g., "Lowe's Home Improvement" (may be NULL) */
    const char *email; /* e.g., "lowes@e.lowes.com" */
} gcloud_v1_gmail_contact_t;

/* ------- Messages.List Sink ------- */
typedef struct {
    const char *id;
    const char *thread_id;
} gcloud_v1_gmail_message_ref_t;

typedef void (*gcloud_v1_gmail_messages_list_cb)(
    void *arg,
    curl_event_request_t *request,
    bool success,
    const gcloud_v1_gmail_message_ref_t *messages,
    size_t num_messages,
    const char *next_page_token,
    uint64_t result_size_estimate
);

curl_sink_interface_t *gcloud_v1_gmail_messages_list_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_messages_list_cb cb,
    void *cb_arg);

/* ------- Messages.Get Sink ------- */
typedef struct {
    const char *id;
    const char *thread_id;
    const char *snippet;
    uint64_t internal_date;

    /* Structural Routing Data */
    const char *delivered_to;
    const char *list_id;
    bool is_group;

    /* Structured, unescaped header fields */
    const char *subject;

    gcloud_v1_gmail_contact_t *from; /* Usually just 1 */

    gcloud_v1_gmail_contact_t *to;
    size_t num_to;

    gcloud_v1_gmail_contact_t *cc;
    size_t num_cc;

    gcloud_v1_gmail_contact_t *bcc;
    size_t num_bcc;

    /* Decoded payload (From raw or snippet) */
    const uint8_t *body_data;
    size_t body_len;

    /* Full Header map (All values are unescaped) */
    const char **header_names;
    const char **header_values;
    size_t num_headers;
} gcloud_v1_gmail_message_t;

typedef void (*gcloud_v1_gmail_message_get_cb)(
    void *arg,
    curl_event_request_t *request,
    bool success,
    const gcloud_v1_gmail_message_t *message
);

curl_sink_interface_t *gcloud_v1_gmail_message_get_sink(
    curl_event_request_t *req,
    bool extract_html_text,
    gcloud_v1_gmail_message_get_cb cb,
    void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif
