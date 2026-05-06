// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/v1/pubsub.h"
#include "a-curl-gcloud-plugin/plugins/token.h"   /* gcloud_token_payload_t */
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ---------------- endpoint helpers ---------------- */

void gcloud_v1_pubsub_build_locational_endpoint(char *buf, size_t buflen, const char *location) {
  snprintf(buf, buflen, "https://%s-pubsub.googleapis.com", location ? location : "us-central1");
}
void gcloud_v1_pubsub_build_regional_endpoint(char *buf, size_t buflen, const char *region) {
  snprintf(buf, buflen, "https://pubsub.%s.rep.googleapis.com", region ? region : "us-central1");
}

/* URL-encode a path segment (conservative: [A-Za-z0-9-_.~] pass-through) */
static inline int _is_unreserved(int c) {
  return (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~');
}
const char *gcloud_v1_pubsub_url_encode_segment(aml_pool_t *pool, const char *segment) {
  if (!segment) return ajson_str_dup(pool, "");
  size_t n = strlen(segment);
  /* Worst case: every byte becomes %XX -> 3x + NUL */
  char *out = aml_pool_alloc(pool, n*3 + 1);
  char *p = out;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)segment[i];
    if (_is_unreserved(c)) {
      *p++ = (char)c;
    } else {
      static const char hex[] = "0123456789ABCDEF";
      *p++ = '%'; *p++ = hex[c >> 4]; *p++ = hex[c & 0xF];
    }
  }
  *p = 0;
  return out;
}

/* ---------------- base64 helpers ---------------- */

static const signed char _b64_rev[256] = {
  /* initialize on first use */
};
static void _b64_init(signed char *tbl) {
  memset(tbl, -1, 256);
  const char *ff = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i=0; i<64; ++i) tbl[(unsigned char)ff[i]] = (signed char)i;
}
static char *_b64_enc(aml_pool_t *pool, const void *data, size_t len) {
  static const char *enc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = ((len + 2)/3)*4;
  char *out = aml_pool_alloc(pool, out_len + 1);
  const unsigned char *in = (const unsigned char *)data;
  char *p = out;
  for (size_t i=0; i<len; i+=3) {
    unsigned v = in[i] << 16;
    if (i+1 < len) v |= in[i+1] << 8;
    if (i+2 < len) v |= in[i+2];
    *p++ = enc[(v >> 18) & 63];
    *p++ = enc[(v >> 12) & 63];
    *p++ = (i+1 < len) ? enc[(v >> 6) & 63] : '=';
    *p++ = (i+2 < len) ? enc[v & 63] : '=';
  }
  *p = 0;
  return out;
}
static unsigned char *_b64_dec(aml_pool_t *pool, const char *b64, size_t *out_len) {
  static signed char tbl[256]; static int inited = 0;
  if (!inited) { _b64_init(tbl); inited = 1; }

  size_t n = strlen(b64);
  if (n % 4 != 0) return NULL;
  size_t out_cap = (n/4)*3;
  unsigned char *out = aml_pool_alloc(pool, out_cap);
  size_t j = 0;

  for (size_t i=0; i<n; i+=4) {
    int a = b64[i]   == '=' ? -2 : tbl[(unsigned char)b64[i]];
    int b = b64[i+1] == '=' ? -2 : tbl[(unsigned char)b64[i+1]];
    int c = b64[i+2] == '=' ? -2 : tbl[(unsigned char)b64[i+2]];
    int d = b64[i+3] == '=' ? -2 : tbl[(unsigned char)b64[i+3]];
    if (a < 0 || b < 0 || (c < -1) || (d < -1)) return NULL;

    unsigned v = ((unsigned)a << 18) | ((unsigned)b << 12) | ((unsigned)((c<0)?0:c) << 6) | (unsigned)((d<0)?0:d);
    out[j++] = (v >> 16) & 0xFF;
    if (c >= 0) out[j++] = (v >> 8) & 0xFF;
    if (d >= 0) out[j++] = v & 0xFF;
  }
  *out_len = j;
  return out;
}

/* ---------------- per-request plugin data ---------------- */

typedef struct {
  curl_event_res_id token_id;
  const char *base_endpoint; /* pool-owned dup */
  ajson_t    *root;          /* request JSON (for POST/PUT) */
  /* For list query params: */
  int page_size;
  const char *page_token;    /* pool-owned dup */
} pubsub_pd_t;

#define PD(req) ((pubsub_pd_t *)(req)->plugin_data)

/* Common on_prepare: add headers and commit JSON if present */
static bool _on_prepare(curl_event_request_t *req) {
  if (!req || !req->plugin_data) return false;
  pubsub_pd_t *pd = PD(req);

  const gcloud_token_payload_t *tok =
      (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, pd->token_id);
  if (!tok || !tok->access_token) {
    fprintf(stderr, "[gcloud.pubsub] missing/invalid token\n");
    return false;
  }

  char hdr[1024];
  snprintf(hdr, sizeof hdr, "Bearer %s", tok->access_token);
  curl_event_request_set_header(req, "Authorization", hdr);
  curl_event_request_set_header(req, "Content-Type", "application/json");
  curl_event_request_set_header(req, "Accept", "application/json");

  if (pd->root) curl_event_request_json_commit(req);

  return true;
}

/* Utility: start a request with method + URL path (relative to base) */
static curl_event_request_t *
_start(curl_event_loop_t *loop, curl_event_res_id token_id,
       const char *base_endpoint, const char *method, const char *path,
       bool want_json_root)
{
  if (!loop || !token_id || !base_endpoint || !*base_endpoint || !method || !path) return NULL;

  curl_event_request_t *req = curl_event_request_init(0);
  if (!req) return NULL;

  size_t n = strlen(base_endpoint) + strlen(path) + 1;
  char *url = aml_pool_alloc(req->pool, n + 1);
  snprintf(url, n + 1, "%s%s", base_endpoint, path);

  curl_event_request_url(req, url);
  curl_event_request_method(req, method);

  pubsub_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof *pd);
  pd->token_id = token_id;
  pd->base_endpoint = aml_pool_strdup(req->pool, base_endpoint);
  pd->page_size = 0; pd->page_token = NULL;
  curl_event_request_plugin_data(req, pd, /*cleanup=*/NULL);

  if (want_json_root) {
    pd->root = curl_event_request_json_begin(req, /*array_root=*/false);
  }

  curl_event_request_depend(req, token_id);
  curl_event_request_on_prepare(req, _on_prepare);
  curl_event_request_low_speed(req, 1024, 60);
  curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);

  return req;
}

/* Append pagination params to URL for list calls */
static void _apply_paging(curl_event_request_t *req) {
  pubsub_pd_t *pd = PD(req);
  if ((pd->page_size <= 0) && (!pd->page_token || !*pd->page_token)) return;

  const char *old = curl_event_request_get_url(req);
  size_t extra = 1 + 32 + (pd->page_token ? strlen(pd->page_token) + 11 : 0);
  char *url = aml_pool_alloc(req->pool, strlen(old) + extra + 1);

  if (strchr(old, '?')) {
    snprintf(url, strlen(old) + extra + 1, "%s%s%s%s%s%d",
             old,
             (pd->page_size > 0 || pd->page_token) ? "&" : "",
             (pd->page_size > 0) ? "pageSize=" : "",
             (pd->page_size > 0) ? "" : "",
             "", 0);
  }

  /* Construct afresh (clearer) */
  const char *q = strchr(old, '?') ? "&" : "?";
  size_t pos = snprintf(url, strlen(old) + 2, "%s", old);
  if (pd->page_size > 0) {
    pos += snprintf(url + pos, extra + 1, "%spageSize=%d", q, pd->page_size);
    q = "&";
  }
  if (pd->page_token && *pd->page_token) {
    pos += snprintf(url + pos, extra + 1, "%spageToken=%s", q, pd->page_token);
  }

  curl_event_request_url(req, url);
}

/* ---------------- Topics ---------------- */

curl_event_request_t *
gcloud_v1_pubsub_topics_create_init(curl_event_loop_t *loop,
                                    curl_event_res_id  token_id,
                                    const char        *base_endpoint,
                                    const char        *project_id,
                                    const char        *topic_id)
{
  if (!project_id || !topic_id) return NULL;
  aml_pool_t *tmp = aml_pool_init(); /* transient for encoding */
  const char *topic_seg = gcloud_v1_pubsub_url_encode_segment(tmp, topic_id);

  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/topics/%s", project_id, topic_seg);

  curl_event_request_t *req =
      _start(loop, token_id, base_endpoint, "PUT", path, /*want_json_root=*/true);

  /* Body is the Topic resource; can be empty object '{}' */
  (void)PD(req)->root;

  aml_pool_destroy(tmp);
  return req;
}

void gcloud_v1_pubsub_topics_create_set_kms_key(curl_event_request_t *req, const char *kms_key_name) {
  if (!req || !kms_key_name) return;
  ajsono_append(PD(req)->root, "kmsKeyName", ajson_encode_str(req->pool, kms_key_name), false);
}
void gcloud_v1_pubsub_topics_create_add_label(curl_event_request_t *req, const char *key, const char *value) {
  if (!req || !key || !value) return;
  ajson_t *labels = ajsono_scan(PD(req)->root, "labels");
  if (!labels) {
    labels = ajsono(req->pool);
    ajsono_append(PD(req)->root, "labels", labels, false);
  }
  ajsono_append(labels, key, ajson_encode_str(req->pool, value), false);
}

curl_event_request_t *
gcloud_v1_pubsub_topics_get_init(curl_event_loop_t *loop,
                                 curl_event_res_id  token_id,
                                 const char        *base_endpoint,
                                 const char        *project_id,
                                 const char        *topic_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *topic_seg = gcloud_v1_pubsub_url_encode_segment(tmp, topic_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/topics/%s", project_id, topic_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "GET", path, false);
  aml_pool_destroy(tmp);
  return req;
}

curl_event_request_t *
gcloud_v1_pubsub_topics_delete_init(curl_event_loop_t *loop,
                                    curl_event_res_id  token_id,
                                    const char        *base_endpoint,
                                    const char        *project_id,
                                    const char        *topic_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *topic_seg = gcloud_v1_pubsub_url_encode_segment(tmp, topic_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/topics/%s", project_id, topic_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "DELETE", path, false);
  aml_pool_destroy(tmp);
  return req;
}

curl_event_request_t *
gcloud_v1_pubsub_topics_list_init(curl_event_loop_t *loop,
                                  curl_event_res_id  token_id,
                                  const char        *base_endpoint,
                                  const char        *project_id)
{
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/topics", project_id);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "GET", path, false);
  return req;
}
void gcloud_v1_pubsub_topics_list_set_page_size(curl_event_request_t *req, int page_size) {
  if (!req) return; PD(req)->page_size = page_size;
  _apply_paging(req);
}
void gcloud_v1_pubsub_topics_list_set_page_token(curl_event_request_t *req, const char *page_token) {
  if (!req) return; PD(req)->page_token = aml_pool_strdup(req->pool, page_token ? page_token : "");
  _apply_paging(req);
}

curl_event_request_t *
gcloud_v1_pubsub_topics_publish_init(curl_event_loop_t *loop,
                                     curl_event_res_id  token_id,
                                     const char        *base_endpoint,
                                     const char        *project_id,
                                     const char        *topic_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *topic_seg = gcloud_v1_pubsub_url_encode_segment(tmp, topic_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/topics/%s:publish", project_id, topic_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "POST", path, true);

  /* Seed messages array */
  ajson_t *msgs = ajsona(req->pool);
  ajsono_append(PD(req)->root, "messages", msgs, false);

  aml_pool_destroy(tmp);
  return req;
}
static ajson_t *_ensure_msgs(curl_event_request_t *req) {
  ajson_t *root = PD(req)->root;
  ajson_t *msgs = ajsono_scan(root, "messages");
  if (!msgs) {
    msgs = ajsona(req->pool);
    ajsono_append(root, "messages", msgs, false);
  }
  return msgs;
}
void gcloud_v1_pubsub_topics_publish_add_message_b64(curl_event_request_t *req,
                                                     const char *data_b64,
                                                     const char **attr_keys,
                                                     const char **attr_vals,
                                                     size_t n_attrs,
                                                     const char *ordering_key)
{
  if (!req || !data_b64) return;
  ajson_t *m = ajsono(req->pool);
  ajsono_append(m, "data", ajson_encode_str(req->pool, data_b64), false);

  if (n_attrs && attr_keys && attr_vals) {
    ajson_t *attrs = ajsono(req->pool);
    for (size_t i=0; i<n_attrs; ++i) if (attr_keys[i] && attr_vals[i])
      ajsono_append(attrs, attr_keys[i], ajson_encode_str(req->pool, attr_vals[i]), false);
    ajsono_append(m, "attributes", attrs, false);
  }
  if (ordering_key && *ordering_key) {
    ajsono_append(m, "orderingKey", ajson_encode_str(req->pool, ordering_key), false);
  }
  ajsona_append(_ensure_msgs(req), m);
}
void gcloud_v1_pubsub_topics_publish_add_message_bytes(curl_event_request_t *req,
                                                       const void *data, size_t len,
                                                       const char **attr_keys,
                                                       const char **attr_vals,
                                                       size_t n_attrs,
                                                       const char *ordering_key)
{
  if (!req || !data) return;
  char *b64 = _b64_enc(req->pool, data, len);
  gcloud_v1_pubsub_topics_publish_add_message_b64(req, b64, attr_keys, attr_vals, n_attrs, ordering_key);
}

/* ---------------- Subscriptions ---------------- */

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_create_init(curl_event_loop_t *loop,
                                           curl_event_res_id  token_id,
                                           const char        *base_endpoint,
                                           const char        *project_id,
                                           const char        *subscription_id,
                                           const char        *topic_full_name)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "PUT", path, true);

  ajsono_append(PD(req)->root, "topic", ajson_encode_str(req->pool, topic_full_name), false);

  aml_pool_destroy(tmp);
  return req;
}
void gcloud_v1_pubsub_subscriptions_create_set_ack_deadline(curl_event_request_t *req, int seconds) {
  if (!req || seconds <= 0) return;
  ajsono_append(PD(req)->root, "ackDeadlineSeconds", ajson_number(req->pool, seconds), false);
}
void gcloud_v1_pubsub_subscriptions_create_enable_exactly_once(curl_event_request_t *req, bool on) {
  if (!req) return;
  ajsono_append(PD(req)->root, "enableExactlyOnceDelivery", on ? ajson_true(req->pool) : ajson_false(req->pool), false);
}
void gcloud_v1_pubsub_subscriptions_create_set_push_endpoint(curl_event_request_t *req, const char *url) {
  if (!req || !url) return;
  ajson_t *pc = ajsono(req->pool);
  ajsono_append(pc, "pushEndpoint", ajson_encode_str(req->pool, url), false);
  ajsono_append(PD(req)->root, "pushConfig", pc, false);
}
void gcloud_v1_pubsub_subscriptions_create_set_filter(curl_event_request_t *req, const char *filter) {
  if (!req || !filter) return;
  ajsono_append(PD(req)->root, "filter", ajson_encode_str(req->pool, filter), false);
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_get_init(curl_event_loop_t *loop,
                                        curl_event_res_id  token_id,
                                        const char        *base_endpoint,
                                        const char        *project_id,
                                        const char        *subscription_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "GET", path, false);
  aml_pool_destroy(tmp);
  return req;
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_delete_init(curl_event_loop_t *loop,
                                           curl_event_res_id  token_id,
                                           const char        *base_endpoint,
                                           const char        *project_id,
                                           const char        *subscription_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "DELETE", path, false);
  aml_pool_destroy(tmp);
  return req;
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_list_init(curl_event_loop_t *loop,
                                         curl_event_res_id  token_id,
                                         const char        *base_endpoint,
                                         const char        *project_id)
{
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions", project_id);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "GET", path, false);
  return req;
}
void gcloud_v1_pubsub_subscriptions_list_set_page_size(curl_event_request_t *req, int page_size) {
  if (!req) return; PD(req)->page_size = page_size; _apply_paging(req);
}
void gcloud_v1_pubsub_subscriptions_list_set_page_token(curl_event_request_t *req, const char *page_token) {
  if (!req) return; PD(req)->page_token = aml_pool_strdup(req->pool, page_token ? page_token : ""); _apply_paging(req);
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_pull_init(curl_event_loop_t *loop,
                                         curl_event_res_id  token_id,
                                         const char        *base_endpoint,
                                         const char        *project_id,
                                         const char        *subscription_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s:pull", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "POST", path, true);
  /* default body */
  ajsono_append(PD(req)->root, "maxMessages", ajson_number(req->pool, 1), false);
  aml_pool_destroy(tmp);
  return req;
}
void gcloud_v1_pubsub_subscriptions_pull_set_max_messages(curl_event_request_t *req, int max_messages) {
  if (!req || max_messages <= 0) return;
  ajsono_append(PD(req)->root, "maxMessages", ajson_number(req->pool, max_messages), true);
}
void gcloud_v1_pubsub_subscriptions_pull_set_return_immediately(curl_event_request_t *req, bool on) {
  if (!req) return;
  ajsono_append(PD(req)->root, "returnImmediately", on ? ajson_true(req->pool) : ajson_false(req->pool), true);
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_ack_init(curl_event_loop_t *loop,
                                        curl_event_res_id  token_id,
                                        const char        *base_endpoint,
                                        const char        *project_id,
                                        const char        *subscription_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s:acknowledge", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "POST", path, true);
  ajson_t *ids = ajsona(req->pool);
  ajsono_append(PD(req)->root, "ackIds", ids, false);
  aml_pool_destroy(tmp);
  return req;
}
void gcloud_v1_pubsub_subscriptions_ack_add_id(curl_event_request_t *req, const char *ack_id) {
  if (!req || !ack_id) return;
  ajson_t *ids = ajsono_scan(PD(req)->root, "ackIds");
  ajsona_append(ids, ajson_encode_str(req->pool, ack_id));
}

curl_event_request_t *
gcloud_v1_pubsub_subscriptions_modify_ack_deadline_init(curl_event_loop_t *loop,
                                                        curl_event_res_id  token_id,
                                                        const char        *base_endpoint,
                                                        const char        *project_id,
                                                        const char        *subscription_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sub_seg = gcloud_v1_pubsub_url_encode_segment(tmp, subscription_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/subscriptions/%s:modifyAckDeadline", project_id, sub_seg);
  curl_event_request_t *req = _start(loop, token_id, base_endpoint, "POST", path, true);
  ajson_t *ids = ajsona(req->pool);
  ajsono_append(PD(req)->root, "ackIds", ids, false);
  aml_pool_destroy(tmp);
  return req;
}
void gcloud_v1_pubsub_subscriptions_modify_ack_deadline_set_seconds(curl_event_request_t *req, int seconds) {
  if (!req || seconds <= 0) return;
  ajsono_append(PD(req)->root, "ackDeadlineSeconds", ajson_number(req->pool, seconds), false);
}
void gcloud_v1_pubsub_subscriptions_modify_ack_deadline_add_id(curl_event_request_t *req, const char *ack_id) {
  if (!req || !ack_id) return;
  ajson_t *ids = ajsono_scan(PD(req)->root, "ackIds");
  ajsona_append(ids, ajson_encode_str(req->pool, ack_id));
}
