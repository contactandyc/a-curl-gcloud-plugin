// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

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
