// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/sinks/v1/embeddings.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

/* -------- shared parse ---------- */
typedef struct {
  float **vecs;
  size_t  n;
  size_t  dim;
  gcloud_v1_embed_stats_t *stats; /* NULL if not requested */
} parsed_out_t;

static bool parse_predictions(aml_pool_t *pool,
                              const char *raw,
                              size_t expected_dim,
                              bool want_stats,
                              parsed_out_t *out)
{
  memset(out, 0, sizeof *out);

  ajson_t *json = ajson_parse_string(pool, raw);
  if (!json || ajson_is_error(json)) return false;

  ajson_t *preds = ajsono_scan(json, "predictions");
  if (!preds || !ajson_is_array(preds)) return false;

  size_t n = ajsona_count(preds);
  float **vecs = aml_pool_alloc(pool, n * sizeof(float *));
  if (!vecs) return false;

  gcloud_v1_embed_stats_t *stats = NULL;
  if (want_stats) {
    stats = aml_pool_calloc(pool, n, sizeof *stats);
    if (!stats) return false;
  }

  size_t dim0 = 0, i = 0;
  for (ajsona_t *el = ajsona_first(preds); el && i < n; el = ajsona_next(el), ++i) {
    ajson_t *emb = ajsono_scan(el->value, "embeddings");
    if (!emb || !ajson_is_object(emb)) return false;

    size_t dim = 0;
    vecs[i] = ajson_extract_float_array(&dim, pool, ajsono_scan(emb, "values"));
    if (!vecs[i]) return false;

    if (expected_dim && dim != expected_dim) return false;
    if (i == 0) dim0 = dim;

    if (want_stats) {
      ajson_t *st = ajsono_scan(emb, "statistics");
      stats[i].token_count = st ? ajsono_scan_int(st, "token_count", 0) : 0;
      /* ajson has no explicit bool getter; coerce via int 0/1 */
      stats[i].truncated   = st ? ajsono_scan_int(st, "truncated", 0)    : 0;
    }
  }

  out->vecs  = vecs;
  out->n     = n;
  out->dim   = expected_dim ? expected_dim : dim0;
  out->stats = stats;
  return true;
}

/* -------- base sink (values only) ---------- */
typedef struct {
  curl_sink_interface_t iface;
  aml_buffer_t *response;
  gcloud_v1_embeddings_complete_cb cb;
  void *arg;
  size_t expected_dim;
} sink_basic_t;

static bool basic_init(curl_sink_interface_t *iface, long) {
  sink_basic_t *s = (void *)iface;
  s->response = aml_buffer_init(2048);
  return s->response != NULL;
}
static size_t basic_write(const void *ptr, size_t size, size_t nmemb,
                          curl_sink_interface_t *iface) {
  sink_basic_t *s = (void *)iface;
  aml_buffer_append(s->response, ptr, size * nmemb);
  return size * nmemb;
}
static void basic_failure(CURLcode res, long http,
                          curl_sink_interface_t *iface,
                          curl_event_request_t *req) {
  sink_basic_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.embed.sink] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->arg, req, false, NULL, 0, 0);
}
static void basic_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_basic_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  parsed_out_t out;
  bool ok = parse_predictions(pool, aml_buffer_data(s->response), s->expected_dim, /*want_stats=*/false, &out);
  aml_buffer_clear(s->response);
  if (!ok) { if (s->cb) s->cb(s->arg, req, false, NULL, 0, 0); return; }
  if (s->cb) s->cb(s->arg, req, true, out.vecs, out.n, out.dim);
}
static void basic_destroy(curl_sink_interface_t *iface) {
  sink_basic_t *s = (void *)iface;
  if (s->response) aml_buffer_destroy(s->response);
}
curl_sink_interface_t *
gcloud_v1_embeddings_sink(curl_event_request_t *req,
                          size_t expected_dim,
                          gcloud_v1_embeddings_complete_cb cb,
                          void *cb_arg) {
  if (!req) return NULL;
  sink_basic_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  if (!s) return NULL;
  s->cb = cb; s->arg = cb_arg; s->expected_dim = expected_dim;
  s->iface.pool     = req->pool;
  s->iface.init     = basic_init;
  s->iface.write    = basic_write;
  s->iface.failure  = basic_failure;
  s->iface.complete = basic_complete;
  s->iface.destroy  = basic_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}

/* -------- sink with stats ---------- */
typedef struct {
  curl_sink_interface_t iface;
  aml_buffer_t *response;
  gcloud_v1_embeddings_complete_with_stats_cb cb;
  void *arg;
  size_t expected_dim;
} sink_stats_t;

static bool stats_init(curl_sink_interface_t *iface, long) {
  sink_stats_t *s = (void *)iface;
  s->response = aml_buffer_init(2048);
  return s->response != NULL;
}
static size_t stats_write(const void *ptr, size_t size, size_t nmemb,
                          curl_sink_interface_t *iface) {
  sink_stats_t *s = (void *)iface;
  aml_buffer_append(s->response, ptr, size * nmemb);
  return size * nmemb;
}
static void stats_failure(CURLcode res, long http,
                          curl_sink_interface_t *iface,
                          curl_event_request_t *req) {
  sink_stats_t *s = (void *)iface;
  fprintf(stderr, "[gcloud.embed.sink+stats] HTTP %ld, CURL %d\n", http, res);
  if (s->cb) s->cb(s->arg, req, false, NULL, 0, 0, NULL);
}
static void stats_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
  sink_stats_t *s = (void *)iface;
  aml_pool_t *pool = iface->pool;
  parsed_out_t out;
  bool ok = parse_predictions(pool, aml_buffer_data(s->response), s->expected_dim, /*want_stats=*/true, &out);
  aml_buffer_clear(s->response);
  if (!ok) { if (s->cb) s->cb(s->arg, req, false, NULL, 0, 0, NULL); return; }
  if (s->cb) s->cb(s->arg, req, true, out.vecs, out.n, out.dim, out.stats);
}
static void stats_destroy(curl_sink_interface_t *iface) {
  sink_stats_t *s = (void *)iface;
  if (s->response) aml_buffer_destroy(s->response);
}
curl_sink_interface_t *
gcloud_v1_embeddings_sink_with_stats(curl_event_request_t *req,
                                     size_t expected_dim,
                                     gcloud_v1_embeddings_complete_with_stats_cb cb,
                                     void *cb_arg) {
  if (!req) return NULL;
  sink_stats_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  if (!s) return NULL;
  s->cb = cb; s->arg = cb_arg; s->expected_dim = expected_dim;
  s->iface.pool     = req->pool;
  s->iface.init     = stats_init;
  s->iface.write    = stats_write;
  s->iface.failure  = stats_failure;
  s->iface.complete = stats_complete;
  s->iface.destroy  = stats_destroy;
  curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
  return (curl_sink_interface_t *)s;
}
