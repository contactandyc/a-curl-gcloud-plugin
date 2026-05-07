// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/v1/gmail.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct {
  curl_event_res_id token_id;
  const char *base_endpoint;
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
    fprintf(stderr, "[gcloud.gmail] missing/invalid token\n");
    return false;
  }

  char hdr[1024];
  snprintf(hdr, sizeof(hdr), "Bearer %s", tok->access_token);
  curl_event_request_set_header(req, "Authorization", hdr);
  curl_event_request_set_header(req, "Content-Type", "application/json");

  return true;
}

static curl_event_request_t *
_start(curl_event_loop_t *loop, curl_event_res_id token_id,
       const char *base_endpoint, const char *method, const char *path)
{
  if (!loop || !token_id || !base_endpoint || !path) return NULL;

  curl_event_request_t *req = curl_event_request_init(0);
  if (!req) return NULL;

  size_t n = strlen(base_endpoint) + strlen(path) + 1;
  char *url = aml_pool_alloc(req->pool, n + 1);
  snprintf(url, n + 1, "%s%s", base_endpoint, path);

  curl_event_request_url(req, url);
  curl_event_request_method(req, method);

  gmail_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof(*pd));
  pd->token_id = token_id;
  pd->base_endpoint = aml_pool_strdup(req->pool, base_endpoint);
  curl_event_request_plugin_data(req, pd, NULL);

  curl_event_request_depend(req, token_id);
  curl_event_request_on_prepare(req, _on_prepare);
  curl_event_request_low_speed(req, 1024, 60);
  curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);

  return req;
}

static void _apply_query_params(curl_event_request_t *req) {
  gmail_pd_t *pd = PD(req);
  const char *old = req->url;

  // A crude but effective URL builder for query params
  size_t extra = 1 + 64;
  if (pd->page_token) extra += strlen(pd->page_token) + 16;
  if (pd->query) extra += strlen(pd->query) + 8; // Note: user must pre-url-encode query
  if (pd->format) extra += strlen(pd->format) + 8;

  char *url = aml_pool_alloc(req->pool, strlen(old) + extra);
  const char *q = strchr(old, '?') ? "&" : "?";
  size_t pos = snprintf(url, strlen(old) + 2, "%s", old);

  if (pd->max_results > 0) {
    pos += snprintf(url + pos, extra, "%smaxResults=%d", q, pd->max_results);
    q = "&";
  }
  if (pd->page_token && *pd->page_token) {
    pos += snprintf(url + pos, extra, "%spageToken=%s", q, pd->page_token);
    q = "&";
  }
  if (pd->query && *pd->query) {
    pos += snprintf(url + pos, extra, "%sq=%s", q, pd->query);
    q = "&";
  }
  if (pd->format && *pd->format) {
    pos += snprintf(url + pos, extra, "%sformat=%s", q, pd->format);
    q = "&";
  }

  curl_event_request_url(req, url);
}

curl_event_request_t *gcloud_v1_gmail_messages_list_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id)
{
  char path[1024];
  snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages", user_id ? user_id : "me");
  return _start(loop, token_id, base_endpoint, "GET", path);
}

void gcloud_v1_gmail_messages_list_set_max_results(curl_event_request_t *req, int max_results) {
  if (!req) return; PD(req)->max_results = max_results; _apply_query_params(req);
}
void gcloud_v1_gmail_messages_list_set_page_token(curl_event_request_t *req, const char *page_token) {
  if (!req) return; PD(req)->page_token = aml_pool_strdup(req->pool, page_token ? page_token : ""); _apply_query_params(req);
}
void gcloud_v1_gmail_messages_list_set_query(curl_event_request_t *req, const char *query) {
  if (!req) return; PD(req)->query = aml_pool_strdup(req->pool, query ? query : ""); _apply_query_params(req);
}

curl_event_request_t *gcloud_v1_gmail_messages_get_init(
    curl_event_loop_t *loop,
    curl_event_res_id token_id,
    const char *base_endpoint,
    const char *user_id,
    const char *message_id)
{
  char path[1024];
  snprintf(path, sizeof(path), "/gmail/v1/users/%s/messages/%s", user_id ? user_id : "me", message_id);
  return _start(loop, token_id, base_endpoint, "GET", path);
}

void gcloud_v1_gmail_messages_get_set_format(curl_event_request_t *req, const char *format) {
  if (!req) return; PD(req)->format = aml_pool_strdup(req->pool, format ? format : ""); _apply_query_params(req);
}
