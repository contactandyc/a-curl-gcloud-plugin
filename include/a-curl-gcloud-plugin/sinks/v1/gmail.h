// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

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
    const char *name;
    const char *email;
} gcloud_v1_gmail_contact_t;

/* ------- Attachment Reference Struct ------- */
typedef struct {
    const char *attachment_id;
    const char *filename;
    const char *mime_type;
    size_t size;
} gcloud_v1_gmail_attachment_ref_t;

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

    const char *delivered_to;
    const char *list_id;
    bool is_group;

    const char *subject;
    gcloud_v1_gmail_contact_t *from;

    gcloud_v1_gmail_contact_t *to;
    size_t num_to;

    gcloud_v1_gmail_contact_t *cc;
    size_t num_cc;

    gcloud_v1_gmail_contact_t *bcc;
    size_t num_bcc;

    const uint8_t *body_data;
    size_t body_len;

    const char **label_ids;
    size_t num_labels;

    gcloud_v1_gmail_attachment_ref_t *attachments;
    size_t num_attachments;

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

/* ------- Attachments.Get Sink ------- */
typedef void (*gcloud_v1_gmail_attachment_get_cb)(
    void *arg,
    curl_event_request_t *request,
    bool success,
    const uint8_t *file_data,
    size_t file_len
);

curl_sink_interface_t *gcloud_v1_gmail_attachment_get_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_attachment_get_cb cb,
    void *cb_arg);

/* ------- Message ID Sink (For Send, Trash, Delete) ------- */
typedef void (*gcloud_v1_gmail_message_id_cb)(
    void *arg,
    curl_event_request_t *request,
    bool success,
    const char *id,
    const char *thread_id
);

curl_sink_interface_t *gcloud_v1_gmail_message_id_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_message_id_cb cb,
    void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif