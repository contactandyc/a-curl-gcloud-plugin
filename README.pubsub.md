Awesome — here’s a **Pub/Sub** addition that matches the style of the embeddings piece I gave you. It implements the essentials (topics, publish, subscriptions, pull, ack, list) and adds **endpoint selection** (global / locational / regional) per the docs you pasted. It keeps the same builder/sink ergonomics and token wiring as your existing plugins.

---

## Scope covered (aligned to your docs)

* **Endpoints**

    * Global: `https://pubsub.googleapis.com`
    * Locational: `https://{location}-pubsub.googleapis.com` (e.g., `us-central1-pubsub.googleapis.com`)
    * Regional: `https://pubsub.{region}.rep.googleapis.com` (e.g., `pubsub.us-central1.rep.googleapis.com`)
* **Core REST methods**

    * **Topics**: `create`, `get`, `list`, `delete`, `publish`
    * **Subscriptions**: `create`, `get`, `list`, `delete`, `pull`, `acknowledge`, `modifyAckDeadline`
* **Request/Response shapes**

    * Publish body: `messages[].data (base64)`, `messages[].attributes{}`, `messages[].orderingKey`
    * Publish response: `messageIds[]`
    * Pull body: `maxMessages`, (`returnImmediately` optional)
    * Pull response: `receivedMessages[].ackId`, `.message{messageId, publishTime, data, attributes, orderingKey}`, `.deliveryAttempt`
* **Helpers**

    * Region-aware endpoint builders
    * URL-encoding for **resource path segments** (topics, subscriptions)
    * Base64 **encode** (for publish) and **decode** (for pull)
    * Paged list helpers (page size/token)
    * Optional subscription fields for `create`: `ackDeadlineSeconds`, `enableExactlyOnceDelivery`, `pushConfig.pushEndpoint`, `filter`

> ⚠️ Notes mirrored from the docs you provided:
> • **Regional** endpoints enforce data-residency (your topic’s **message storage policy** must allow the region).
> • Resource names may include special characters — we **URL-encode topic/subscription IDs** when composing paths.
> • Exactly-once delivery requires enabling it on the subscription.

---

# 1) Plugin API — header

**`include/a-curl-gcloud-plugin/plugins/v1/pubsub.h`**

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

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
```

---

# 2) Plugin implementation

**`src/plugins/v1/pubsub.c`**

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

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
```

---

# 3) Sinks — header

**`include/a-curl-gcloud-plugin/sinks/v1/pubsub.h`**

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

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
```

---

# 4) Sinks — implementation

**`src/sinks/v1/pubsub.c`**

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

#include "a-curl-gcloud-plugin/sinks/v1/pubsub.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

/* ---- shared: response buffer sink scaffold ---- */
typedef struct {
  curl_sink_interface_t iface;
  aml_buffer_t *resp;
  void *cb;
  void *cb_arg;
} base_sink_t;

static bool s_init(curl_sink_interface_t *iface, long) {
  base_sink_t *s = (void *)iface;
  s->resp = aml_buffer_init(2048);
  return s->resp != NULL;
}
static size_t s_write(const void *ptr, size_t sz, size_t nm, curl_sink_interface_t *iface) {
  base_sink_t *s = (void *)iface;
  aml_buffer_append(s->resp, ptr, sz * nm);
  return sz * nm;
}
static void s_destroy(curl_sink_interface_t *iface) {
  base_sink_t *s = (void *)iface;
  if (s->resp) aml_buffer_destroy(s->resp);
}

/* ---- publish sink ---- */
typedef struct {
  base_sink_t b;
  gcloud_v1_pubsub_publish_cb cb;
} sink_publish_t;

static void publish_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_publish_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.pubsub.publish] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0);
}
static void publish_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_publish_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if (!json || ajson_is_error(json)) { if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0); return; }

  ajson_t *ids = ajsono_scan(json, "messageIds");
  size_t n = ids && ajson_is_array(ids) ? ajsona_count(ids) : 0;
  const char **out = n ? aml_pool_alloc(pool, n * sizeof(char *)) : NULL;
  size_t i=0;
  if (ids && ajson_is_array(ids)) {
    for (ajsona_t *el = ajsona_first(ids); el; el = ajsona_next(el)) {
      out[i++] = ajson_to_new_cstring(pool, el->value);
    }
  }
  if (s->cb) s->cb(s->b.cb_arg, req, true, out, n);
}
curl_sink_interface_t *
gcloud_v1_pubsub_publish_sink(curl_event_request_t *req,
                              gcloud_v1_pubsub_publish_cb cb,
                              void *cb_arg)
{
  if (!req) return NULL;
  sink_publish_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg;
  s->b.iface.pool    = req->pool;
  s->b.iface.init    = s_init;
  s->b.iface.write   = s_write;
  s->b.iface.failure = publish_failure;
  s->b.iface.complete= publish_complete;
  s->b.iface.destroy = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}

/* ---- pull sink ---- */
typedef struct {
  base_sink_t b;
  gcloud_v1_pubsub_pull_cb cb;
} sink_pull_t;

/* minimal base64 decode: reuse one from pubsub.c by duplication isn't ideal; implement local */
static const signed char _rev_tbl_local_init_val = 0; /* placeholder */

static void pull_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_pull_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.pubsub.pull] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0);
}
/* tiny base64 decode, same algorithm as plugin; duplicated for decoupling */
static unsigned char *b64dec(aml_pool_t *pool, const char *b64, size_t *out_len) {
  static signed char tbl[256]; static int inited = 0;
  if (!inited) {
    memset(tbl, -1, 256);
    const char *ff = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i=0; i<64; ++i) tbl[(unsigned char)ff[i]] = (signed char)i;
    inited = 1;
  }
  size_t n = strlen(b64);
  if (n % 4 != 0) { *out_len = 0; return NULL; }
  size_t cap = (n/4)*3;
  unsigned char *buf = aml_pool_alloc(pool, cap);
  size_t j=0;
  for (size_t i=0; i<n; i+=4) {
    int a = b64[i]   == '=' ? -2 : tbl[(unsigned char)b64[i]];
    int b = b64[i+1] == '=' ? -2 : tbl[(unsigned char)b64[i+1]];
    int c = b64[i+2] == '=' ? -2 : tbl[(unsigned char)b64[i+2]];
    int d = b64[i+3] == '=' ? -2 : tbl[(unsigned char)b64[i+3]];
    if (a < 0 || b < 0 || (c < -1) || (d < -1)) { *out_len = 0; return NULL; }
    unsigned v = ((unsigned)a << 18) | ((unsigned)b << 12) | ((unsigned)((c<0)?0:c) << 6) | (unsigned)((d<0)?0:d);
    buf[j++] = (v >> 16) & 0xFF;
    if (c >= 0) buf[j++] = (v >> 8) & 0xFF;
    if (d >= 0) buf[j++] = v & 0xFF;
  }
  *out_len = j;
  return buf;
}
static void pull_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_pull_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if (!json || ajson_is_error(json)) { if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0); return; }

  ajson_t *arr = ajsono_scan(json, "receivedMessages");
  size_t n = arr && ajson_is_array(arr) ? ajsona_count(arr) : 0;
  gcloud_v1_pubsub_received_message_t *msgs = n ? aml_pool_calloc(pool, n, sizeof *msgs) : NULL;

  size_t i = 0;
  for (ajsona_t *el = ajsona_first(arr); el; el = ajsona_next(el), ++i) {
    gcloud_v1_pubsub_received_message_t *m = &msgs[i];
    m->ack_id = ajsono_scan_str(el->value, "ackId", NULL);

    ajson_t *msg = ajsono_scan(el->value, "message");
    if (!msg) continue;

    m->message_id   = ajsono_scan_str(msg, "messageId", NULL);
    m->publish_time = ajsono_scan_str(msg, "publishTime", NULL);
    m->ordering_key = ajsono_scan_str(msg, "orderingKey", NULL);

    /* attributes map -> parallel arrays */
    ajson_t *attrs = ajsono_scan(msg, "attributes");
    if (attrs && ajson_is_object(attrs)) {
      size_t ac = ajsono_count(attrs);
      m->attr_keys = aml_pool_alloc(pool, ac * sizeof(char *));
      m->attr_vals = aml_pool_alloc(pool, ac * sizeof(char *));
      m->num_attributes = 0;
      for (ajsono_t *kv = ajsono_first(attrs); kv; kv = ajsono_next(kv)) {
        m->attr_keys[m->num_attributes] = ajson_to_new_cstring(pool, kv->key);
        m->attr_vals[m->num_attributes] = ajson_to_new_cstring(pool, kv->value);
        m->num_attributes++;
      }
    }

    const char *b64 = ajsono_scan_str(msg, "data", NULL);
    m->data_b64 = b64;
    if (b64) {
      size_t L=0; unsigned char *bytes = b64dec(pool, b64, &L);
      m->data = bytes; m->data_len = L;
    }

    /* deliveryAttempt may be present at envelope level */
    m->delivery_attempt = ajsono_scan_int(el->value, "deliveryAttempt", 0);
  }

  if (s->cb) s->cb(s->b.cb_arg, req, true, msgs, n);
}
curl_sink_interface_t *
gcloud_v1_pubsub_pull_sink(curl_event_request_t *req,
                           gcloud_v1_pubsub_pull_cb cb,
                           void *cb_arg)
{
  if (!req) return NULL;
  sink_pull_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg;
  s->b.iface.pool     = req->pool;
  s->b.iface.init     = s_init;
  s->b.iface.write    = s_write;
  s->b.iface.failure  = pull_failure;
  s->b.iface.complete = pull_complete;
  s->b.iface.destroy  = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}

/* ---- list sinks ---- */
typedef struct {
  base_sink_t b;
  gcloud_v1_pubsub_list_cb cb;
  const char *array_field; /* "topics" or "subscriptions" */
} sink_list_t;

static void list_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_list_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.pubsub.list] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0, NULL);
}
static void list_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_list_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if (!json || ajson_is_error(json)) { if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0, NULL); return; }

  ajson_t *arr = ajsono_scan(json, s->array_field);
  size_t n = arr && ajson_is_array(arr) ? ajsona_count(arr) : 0;
  const char **names = n ? aml_pool_alloc(pool, n * sizeof(char *)) : NULL;

  size_t i = 0;
  if (arr) {
    for (ajsona_t *el = ajsona_first(arr); el; el = ajsona_next(el)) {
      const char *name = ajsono_scan_str(el->value, "name", NULL);
      names[i++] = name ? name : "";
    }
  }
  const char *next_tok = ajsono_scan_str(json, "nextPageToken", NULL);
  if (s->cb) s->cb(s->b.cb_arg, req, true, names, n, next_tok);
}
curl_sink_interface_t *
gcloud_v1_pubsub_topics_list_sink(curl_event_request_t *req,
                                  gcloud_v1_pubsub_list_cb cb, void *cb_arg)
{
  if (!req) return NULL;
  sink_list_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg; s->array_field = "topics";
  s->b.iface.pool     = req->pool;
  s->b.iface.init     = s_init;
  s->b.iface.write    = s_write;
  s->b.iface.failure  = list_failure;
  s->b.iface.complete = list_complete;
  s->b.iface.destroy  = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}
curl_sink_interface_t *
gcloud_v1_pubsub_subscriptions_list_sink(curl_event_request_t *req,
                                         gcloud_v1_pubsub_list_cb cb, void *cb_arg)
{
  if (!req) return NULL;
  sink_list_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg; s->array_field = "subscriptions";
  s->b.iface.pool     = req->pool;
  s->b.iface.init     = s_init;
  s->b.iface.write    = s_write;
  s->b.iface.failure  = list_failure;
  s->b.iface.complete = list_complete;
  s->b.iface.destroy  = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}

/* ---- name / empty sinks ---- */
typedef struct { base_sink_t b; gcloud_v1_pubsub_name_cb cb; } sink_name_t;
static void name_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_name_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.pubsub.name] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->b.cb_arg, req, false, NULL);
}
static void name_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_name_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if (!json || ajson_is_error(json)) { if (s->cb) s->cb(s->b.cb_arg, req, false, NULL); return; }
  const char *name = ajsono_scan_str(json, "name", NULL);
  if (s->cb) s->cb(s->b.cb_arg, req, true, name);
}
curl_sink_interface_t *
gcloud_v1_pubsub_name_sink(curl_event_request_t *req,
                           gcloud_v1_pubsub_name_cb cb, void *cb_arg)
{
  if (!req) return NULL;
  sink_name_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg;
  s->b.iface.pool     = req->pool;
  s->b.iface.init     = s_init;
  s->b.iface.write    = s_write;
  s->b.iface.failure  = name_failure;
  s->b.iface.complete = name_complete;
  s->b.iface.destroy  = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}

typedef struct { base_sink_t b; gcloud_v1_pubsub_empty_cb cb; } sink_empty_t;
static void empty_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_empty_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.pubsub.empty] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->b.cb_arg, req, false);
}
static void empty_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_empty_t *s = (void *)iface;
  /* 2xx with {} or empty body is success */
  if (s->cb) s->cb(s->b.cb_arg, req, true);
}
curl_sink_interface_t *
gcloud_v1_pubsub_empty_sink(curl_event_request_t *req,
                            gcloud_v1_pubsub_empty_cb cb, void *cb_arg)
{
  if (!req) return NULL;
  sink_empty_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb = cb; s->b.cb_arg = cb_arg;
  s->b.iface.pool     = req->pool;
  s->b.iface.init     = s_init;
  s->b.iface.write    = s_write;
  s->b.iface.failure  = empty_failure;
  s->b.iface.complete = empty_complete;
  s->b.iface.destroy  = s_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}
```

---

## 5) Quick usage examples

### A. Pick an endpoint

```c
/* Global (auto-routes per Google Cloud rules) */
const char *BASE = gcloud_v1_pubsub_endpoint_global();

/* Or locational */
char BASE_LOC[128];
gcloud_v1_pubsub_build_locational_endpoint(BASE_LOC, sizeof BASE_LOC, "us-west1");   // https://us-west1-pubsub.googleapis.com

/* Or regional (data residency; see message storage policy requirements) */
char BASE_REG[128];
gcloud_v1_pubsub_build_regional_endpoint(BASE_REG, sizeof BASE_REG, "us-central1"); // https://pubsub.us-central1.rep.googleapis.com
```

### B. Create a topic

```c
curl_event_request_t *req =
  gcloud_v1_pubsub_topics_create_init(loop, token_res, BASE, "my-proj", "orders.topic");
gcloud_v1_pubsub_topics_create_add_label(req, "env", "prod");
gcloud_v1_pubsub_name_sink(req, on_topic_ready, ctx);
gcloud_v1_pubsub_submit(loop, req, 0);
```

### C. Create a subscription

```c
curl_event_request_t *req =
  gcloud_v1_pubsub_subscriptions_create_init(
    loop, token_res, BASE, "my-proj", "orders.sub",
    "projects/my-proj/topics/orders.topic");

gcloud_v1_pubsub_subscriptions_create_set_ack_deadline(req, 20);
gcloud_v1_pubsub_subscriptions_create_enable_exactly_once(req, true);
gcloud_v1_pubsub_name_sink(req, on_sub_ready, ctx);
gcloud_v1_pubsub_submit(loop, req, 0);
```

### D. Publish messages

```c
curl_event_request_t *req =
  gcloud_v1_pubsub_topics_publish_init(loop, token_res, BASE, "my-proj", "orders.topic");

const char *keys[] = {"source","type"};
const char *vals[] = {"checkout","created"};
const char payload[] = "{\"order_id\":1234}";

gcloud_v1_pubsub_topics_publish_add_message_bytes(req, payload, sizeof payload - 1,
                                                  keys, vals, 2,
                                                  /*ordering_key=*/"user-42");

gcloud_v1_pubsub_publish_sink(req, on_publish_done, ctx);
gcloud_v1_pubsub_submit(loop, req, 0);
```

### E. Pull & Ack

```c
/* Pull up to 10 messages */
curl_event_request_t *pull =
  gcloud_v1_pubsub_subscriptions_pull_init(loop, token_res, BASE, "my-proj", "orders.sub");
gcloud_v1_pubsub_subscriptions_pull_set_max_messages(pull, 10);
gcloud_v1_pubsub_pull_sink(pull, on_pull, ctx);
gcloud_v1_pubsub_submit(loop, pull, 0);

/* In on_pull(...): decode bytes are provided already; to ack: */
void on_pull(void *arg, curl_event_request_t *req, bool ok,
             const gcloud_v1_pubsub_received_message_t *msgs, size_t n) {
  if (!ok || n==0) return;
  curl_event_loop_t *loop = /*...*/;
  curl_event_res_id token_res = /*...*/;

  curl_event_request_t *ack =
    gcloud_v1_pubsub_subscriptions_ack_init(loop, token_res, gcloud_v1_pubsub_endpoint_global(),
                                            "my-proj", "orders.sub");

  for (size_t i=0; i<n; ++i)
    if (msgs[i].ack_id) gcloud_v1_pubsub_subscriptions_ack_add_id(ack, msgs[i].ack_id);

  gcloud_v1_pubsub_empty_sink(ack, on_ack_done, arg);
  gcloud_v1_pubsub_submit(loop, ack, 0);
}
```

---

## 6) Why this matches the Pub/Sub docs you provided

* **Both REST and endpoint topologies supported**: you can target **global**, **locational**, or **regional** hosts exactly as documented, and you can build the host value from a `region`/`location` string.
* **Resource names & URL encoding**: topic/subscription IDs are encoded as individual **path segments** so reserved characters don’t break requests.
* **Publish**: request shape (`messages[].data` base64, `attributes`, `orderingKey`) and response (`messageIds[]`) are implemented.
* **Pull**: request (`maxMessages`, optional `returnImmediately`) and response parsing (`receivedMessages[].ackId`, `message{...}`, `deliveryAttempt`) implemented, with **base64 decode** for `data`.
* **Ack & modifyAckDeadline** flows\*\* are covered.
* **List & pagination** for both topics and subscriptions.
* **Subscription create** supports the commonly used fields from the REST reference: `topic`, `ackDeadlineSeconds`, `enableExactlyOnceDelivery`, `pushConfig.pushEndpoint`, `filter`.

---

### Small integration checklist

* The same **token payload** used for embeddings works here (`gcloud_token_payload_t` with `access_token`).
* If you plan to support **schemas**, **snapshots**, or **IAM** methods (`getIamPolicy`, `setIamPolicy`, `testIamPermissions`) later, we can extend with more thin builders/sinks using the same patterns as above.
* If you want to honor `CLOUDSDK_API_ENDPOINT_OVERRIDES_PUBSUB` automatically, we can add a tiny helper that checks the env var and uses it as the `base_endpoint` default (your code can also just pass it in when present).

If you want me to add **StreamingPull** (over gRPC) or **schema** endpoints next, tell me which one you prefer and I’ll wire it up in the same style.
