// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_V1_GMAIL_H
#define A_CURL_GCLOUD_PLUGIN_V1_GMAIL_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char *gcloud_v1_gmail_endpoint(void) {
    return "https://gmail.googleapis.com";
}

/* ---------- Messages.List (Delta Sync / Search) ---------- */
curl_event_request_t *gcloud_v1_gmail_messages_list_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id); /* usually "me" */

void gcloud_v1_gmail_messages_list_set_max_results(curl_event_request_t *req, int max_results);
void gcloud_v1_gmail_messages_list_set_page_token(curl_event_request_t *req, const char *page_token);
void gcloud_v1_gmail_messages_list_set_query(curl_event_request_t *req, const char *query);

/* ---------- Messages.Get (Download Body) ---------- */
curl_event_request_t *gcloud_v1_gmail_messages_get_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id,
    const char *message_id);

void gcloud_v1_gmail_messages_get_set_format(curl_event_request_t *req, const char *format);

/* ---------- Attachments.Get (Download File) ---------- */
curl_event_request_t *gcloud_v1_gmail_attachments_get_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id,
    const char *message_id,
    const char *attachment_id);

/* ---------- Messages.Send (Send Email) ---------- */
curl_event_request_t *gcloud_v1_gmail_messages_send_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id);

void gcloud_v1_gmail_messages_send_set_raw(curl_event_request_t *req, const char *websafe_base64_mime);

/* ---------- Messages.Trash (Move to Trash) ---------- */
curl_event_request_t *gcloud_v1_gmail_messages_trash_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id,
    const char *message_id);

/* ---------- Messages.Delete (Permanent Delete) ---------- */
curl_event_request_t *gcloud_v1_gmail_messages_delete_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id,
    const char *message_id);

/* Submit helper */
static inline curl_event_request_t *gcloud_v1_gmail_submit(curl_event_loop_t *loop, curl_event_request_t *req, int priority) {
    return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_V1_GMAIL_H */