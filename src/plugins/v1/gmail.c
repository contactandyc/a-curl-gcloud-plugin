// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <string.h>

/* Per-request plugin data */
typedef struct {
    curl_event_res_id token_id;
    const char *base_endpoint;
    const char *path;

    /* Query parameters */
    int max_results;
    const char *page_token;
    const char *query;
    const char *format;
} gmail_pd_t;

#define PD(req) ((gmail_pd_t *)(req)->plugin_data)

static bool _on_prepare(curl_event_request_t *req) {
    if (!req || !req->plugin_data) return false;
    gmail_pd_t *pd = PD(req);

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, pd->token_id);
    if (!tok || !tok->access_token) {
        fprintf(stderr, "[gcloud.gmail] missing/invalid token payload\n");
        return false;
    }

    char hdr[1024];
    snprintf(hdr, sizeof(hdr), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", hdr);
    curl_event_request_set_header(req, "Content-Type", "application/json");

    size_t extra = strlen(pd->base_endpoint) + strlen(pd->path) + 128;
    if (pd->page_token) extra += strlen(pd->page_token);
    if (pd->query) extra += strlen(pd->query);
    if (pd->format) extra += strlen(pd->format);

    char *url = aml_pool_alloc(req->pool, extra);
    size_t pos = snprintf(url, extra, "%s%s", pd->base_endpoint, pd->path);
    const char *q = "?";

    if (pd->max_results > 0) {
        pos += snprintf(url + pos, extra - pos, "%smaxResults=%d", q, pd->max_results);
        q = "&";
    }
    if (pd->page_token && *pd->page_token) {
        pos += snprintf(url + pos, extra - pos, "%spageToken=%s", q, pd->page_token);
        q = "&";
    }
    if (pd->query && *pd->query) {
        pos += snprintf(url + pos, extra - pos, "%sq=%s", q, pd->query);
        q = "&";
    }
    if (pd->format && *pd->format) {
        pos += snprintf(url + pos, extra - pos, "%sformat=%s", q, pd->format);
    }

    curl_event_request_url(req, url);
    return true;
}

static curl_event_request_t *
_start(curl_event_loop_t *loop, curl_event_res_id token_id,
       const char *base_endpoint, const char *method, const char *path, double weight)
{
    if (!loop || !token_id || !base_endpoint || !path) return NULL;

    curl_event_request_t *req = curl_event_request_init(0);
    if (!req) return NULL;

    char *url = aml_pool_strdupf(req->pool, "%s%s", base_endpoint, path);
    curl_event_request_url(req, url);
    curl_event_request_method(req, method);

    gmail_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof(*pd));
    pd->token_id = token_id;
    pd->base_endpoint = aml_pool_strdup(req->pool, base_endpoint);
    pd->path = aml_pool_strdup(req->pool, path);
    curl_event_request_plugin_data(req, pd, NULL);

    curl_event_request_depend(req, token_id);
    curl_event_request_on_prepare(req, _on_prepare);

    curl_event_request_weighted_rate_limit(req, "gmail_api", false, weight);
    curl_event_request_low_speed(req, 1024, 60);
    curl_event_request_enable_retries(req, 5, 2.0, 500, 30000, true);

    return req;
}

curl_event_request_t *gcloud_v1_gmail_messages_list_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages", user_id ? user_id : "me");
    return _start(loop, token_id, base_endpoint, "GET", path, 1.0);
}

void gcloud_v1_gmail_messages_list_set_max_results(curl_event_request_t *req, int max_results) {
    if (!req) return; PD(req)->max_results = max_results;
}

void gcloud_v1_gmail_messages_list_set_page_token(curl_event_request_t *req, const char *page_token) {
    if (!req) return; PD(req)->page_token = aml_pool_strdup(req->pool, page_token ? page_token : "");
}

void gcloud_v1_gmail_messages_list_set_query(curl_event_request_t *req, const char *query) {
    if (!req) return; PD(req)->query = aml_pool_strdup(req->pool, query ? query : "");
}

curl_event_request_t *gcloud_v1_gmail_messages_get_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id, const char *message_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/%s", user_id ? user_id : "me", message_id);
    return _start(loop, token_id, base_endpoint, "GET", path, 5.0);
}

void gcloud_v1_gmail_messages_get_set_format(curl_event_request_t *req, const char *format) {
    if (!req) return; PD(req)->format = aml_pool_strdup(req->pool, format ? format : "");
}

curl_event_request_t *gcloud_v1_gmail_attachments_get_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id,
    const char *message_id, const char *attachment_id)
{
    char path[2048];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/%s/attachments/%s",
             user_id ? user_id : "me", message_id, attachment_id);
    return _start(loop, token_id, base_endpoint, "GET", path, 5.0);
}

curl_event_request_t *gcloud_v1_gmail_messages_send_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/send", user_id ? user_id : "me");
    return _start(loop, token_id, base_endpoint, "POST", path, 100.0);
}

void gcloud_v1_gmail_messages_send_set_raw(curl_event_request_t *req, const char *websafe_base64_mime) {
    if (!req) return;
    ajson_t *root = curl_event_request_json_begin(req, false);
    ajsono_append(root, "raw", ajson_encode_str(req->pool, websafe_base64_mime), false);
    curl_event_request_json_commit(req);
}

curl_event_request_t *gcloud_v1_gmail_messages_trash_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id, const char *message_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/%s/trash", user_id ? user_id : "me", message_id);
    return _start(loop, token_id, base_endpoint, "POST", path, 5.0);
}

curl_event_request_t *gcloud_v1_gmail_messages_delete_init(
    curl_event_loop_t *loop, curl_event_res_id token_id,
    const char *base_endpoint, const char *user_id, const char *message_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/%s", user_id ? user_id : "me", message_id);
    return _start(loop, token_id, base_endpoint, "DELETE", path, 5.0);
}