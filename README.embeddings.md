Absolutely. Based on the Vertex AI “Text embeddings API” spec you pasted, here’s what your **a-curl‑gcloud‑plugin** needs so it truly matches Google’s surface while keeping the **a‑curl‑openai‑plugin** style:

---

## What changed (concise)

* **Region-aware endpoint**: `https://${REGION}-aiplatform.googleapis.com/.../locations/${REGION}/...` (not hard‑coded `us-central1`).
* **Instance fields**: support `content`, **optional** `task_type`, **optional** `title` (title valid for `RETRIEVAL_DOCUMENT`).
* **Parameters**: support `parameters.autoTruncate` (bool) and `parameters.outputDimensionality` (int).
* **Model quirk**: **enforce single instance** when `model_id == "gemini-embedding-001"`.
* **Task-type helpers**: string constants + convenience adders for common tasks.
* **Stats parsing**: optional sink variant returns `token_count` and `truncated` per prediction.
* Kept the **builder/sink** ergonomics and dependency wiring identical to your OpenAI plugin.

---

## Drop‑in updates

> These are **replacements** for the four files I shared earlier, now aligned with the Vertex doc.

### `include/a-curl-gcloud-plugin/plugins/v1/embeddings.h`

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

#ifndef A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H
#define A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H

#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task type string constants (from Vertex docs) */
#define GCLOUD_EMBED_TASK_RETRIEVAL_QUERY     "RETRIEVAL_QUERY"
#define GCLOUD_EMBED_TASK_RETRIEVAL_DOCUMENT  "RETRIEVAL_DOCUMENT"
#define GCLOUD_EMBED_TASK_SEMANTIC_SIMILARITY "SEMANTIC_SIMILARITY"
#define GCLOUD_EMBED_TASK_CLASSIFICATION      "CLASSIFICATION"
#define GCLOUD_EMBED_TASK_CLUSTERING          "CLUSTERING"
#define GCLOUD_EMBED_TASK_QUESTION_ANSWERING  "QUESTION_ANSWERING"
#define GCLOUD_EMBED_TASK_FACT_VERIFICATION   "FACT_VERIFICATION"
#define GCLOUD_EMBED_TASK_CODE_RETRIEVAL_QUERY "CODE_RETRIEVAL_QUERY"

/**
 * Region-aware builder:
 *   https://{region}-aiplatform.googleapis.com/v1/projects/{project}/locations/{region}/publishers/google/models/{model}:predict
 *
 * Dependencies:
 *   token_id -> gcloud_token_payload_t* (see token.h)
 */
curl_event_request_t *
gcloud_v1_embeddings_init_region(curl_event_loop_t *loop,
                                 curl_event_res_id  token_id,
                                 const char        *project_id,
                                 const char        *region,
                                 const char        *model_id);

/* Back-compat helper (defaults region to "us-central1") */
static inline curl_event_request_t *
gcloud_v1_embeddings_init(curl_event_loop_t *loop,
                          curl_event_res_id  token_id,
                          const char        *project_id,
                          const char        *model_id)
{
  return gcloud_v1_embeddings_init_region(loop, token_id, project_id, "us-central1", model_id);
}

/* Instances — simple API */
void gcloud_v1_embeddings_add_text (curl_event_request_t *req, const char *text);
void gcloud_v1_embeddings_add_texts(curl_event_request_t *req, const char **texts, size_t n);
/* Instances — full control (content + task_type + title) */
void gcloud_v1_embeddings_add_instance(curl_event_request_t *req,
                                       const char *content,
                                       const char *task_type /*nullable*/,
                                       const char *title     /*nullable*/);
/* Instances — convenience */
static inline void gcloud_v1_embeddings_add_query(curl_event_request_t *req, const char *content) {
  gcloud_v1_embeddings_add_instance(req, content, GCLOUD_EMBED_TASK_RETRIEVAL_QUERY, NULL);
}
static inline void gcloud_v1_embeddings_add_document(curl_event_request_t *req,
                                                     const char *content,
                                                     const char *title /*nullable*/) {
  gcloud_v1_embeddings_add_instance(req, content, GCLOUD_EMBED_TASK_RETRIEVAL_DOCUMENT, title);
}

/* Parameters */
void gcloud_v1_embeddings_set_output_dimensionality(curl_event_request_t *req, int dimensions);
void gcloud_v1_embeddings_set_auto_truncate       (curl_event_request_t *req, bool on);

/* Submit helper */
static inline curl_event_request_t *
gcloud_v1_embeddings_submit(curl_event_loop_t *loop,
                            curl_event_request_t *req,
                            int priority) {
  return curl_event_request_submit(loop, req, priority);
}

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_V1_EMBEDDINGS_H */
```

---

### `src/plugins/v1/embeddings.c`

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

#include "a-curl-gcloud-plugin/plugins/v1/embeddings.h"
#include "a-curl-gcloud-plugin/plugins/token.h"   /* gcloud_token_payload_t */
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"

#include <stdio.h>
#include <string.h>

static const char *URL_FMT =
  "https://%s-aiplatform.googleapis.com/v1/projects/%s/locations/%s/publishers/google/models/%s:predict";

/* Per-request plugin-data */
typedef struct {
  curl_event_res_id token_id;
  const char       *model_id;   /* pool-owned dup */
  ajson_t          *root;       /* object root */
  ajson_t          *instances;  /* [] */
  ajson_t          *parameters; /* {} (optional) */
} gcloud_v1_embeddings_pd_t;

#define PD(req) ((gcloud_v1_embeddings_pd_t *)(req)->plugin_data)

/* on_prepare: inject Authorization + Content-Type, enforce gemini single-instance, commit JSON */
static bool _on_prepare(curl_event_request_t *req) {
  if (!req || !req->plugin_data) return false;
  gcloud_v1_embeddings_pd_t *pd = PD(req);

  const gcloud_token_payload_t *tok =
    (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, pd->token_id);
  if (!tok || !tok->access_token) {
    fprintf(stderr, "[gcloud.embed] missing/invalid token payload\n");
    return false;
  }

  /* gemini-embedding-001: one instance per request */
  if (pd->model_id && strcmp(pd->model_id, "gemini-embedding-001") == 0) {
    int n = pd->instances ? ajsona_count(pd->instances) : 0;
    if (n > 1) {
      fprintf(stderr, "[gcloud.embed] gemini-embedding-001 allows exactly one instance (got %d)\n", n);
      return false;
    }
  }

  char hdr[1024];
  snprintf(hdr, sizeof hdr, "Bearer %s", tok->access_token);
  curl_event_request_set_header(req, "Authorization", hdr);
  curl_event_request_set_header(req, "Content-Type", "application/json");
  curl_event_request_set_header(req, "Accept", "application/json");

  curl_event_request_json_commit(req);
  return true;
}

/* Builder */
curl_event_request_t *
gcloud_v1_embeddings_init_region(curl_event_loop_t *loop,
                                 curl_event_res_id  token_id,
                                 const char        *project_id,
                                 const char        *region,
                                 const char        *model_id)
{
  if (!loop || !token_id || !project_id || !*project_id || !region || !*region || !model_id || !*model_id) {
    fprintf(stderr, "[gcloud.embed] invalid args\n");
    return NULL;
  }

  curl_event_request_t *req = curl_event_request_init(0);
  if (!req) return NULL;

  char url[1024];
  snprintf(url, sizeof url, URL_FMT, region, project_id, region, model_id);
  curl_event_request_url(req, url);
  curl_event_request_method(req, "POST");

  /* plugin-data */
  gcloud_v1_embeddings_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof *pd);
  pd->token_id = token_id;
  pd->model_id = aml_pool_strdup(req->pool, model_id);
  curl_event_request_plugin_data(req, pd, /*cleanup=*/NULL);

  /* JSON seed */
  ajson_t *root = curl_event_request_json_begin(req, /*array_root=*/false);
  pd->root = root;
  pd->instances = ajsona(req->pool);
  ajsono_append(root, "instances", pd->instances, false);
  /* parameters created lazily */

  /* deps + defaults */
  curl_event_request_depend(req, token_id);
  curl_event_request_on_prepare(req, _on_prepare);
  curl_event_request_low_speed(req, 1024, 60);
  curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);

  return req;
}

/* helpers */
static ajson_t *ensure_params(curl_event_request_t *req) {
  gcloud_v1_embeddings_pd_t *pd = PD(req);
  if (!pd->parameters) {
    pd->parameters = ajsono(req->pool);
    ajsono_append(pd->root, "parameters", pd->parameters, false);
  }
  return pd->parameters;
}

/* Instances (simple) */
void gcloud_v1_embeddings_add_text(curl_event_request_t *req, const char *text) {
  if (!req || !text) return;
  ajson_t *obj = ajsono(req->pool);
  ajsono_append(obj, "content", ajson_encode_str(req->pool, text), false);
  ajsona_append(PD(req)->instances, obj);
}
void gcloud_v1_embeddings_add_texts(curl_event_request_t *req, const char **texts, size_t n) {
  if (!req || !texts) return;
  for (size_t i = 0; i < n; ++i) if (texts[i]) gcloud_v1_embeddings_add_text(req, texts[i]);
}

/* Instances (full) */
void gcloud_v1_embeddings_add_instance(curl_event_request_t *req,
                                       const char *content,
                                       const char *task_type,
                                       const char *title)
{
  if (!req || !content) return;
  ajson_t *obj = ajsono(req->pool);
  ajsono_append(obj, "content", ajson_encode_str(req->pool, content), false);
  if (task_type && *task_type)
    ajsono_append(obj, "task_type", ajson_encode_str(req->pool, task_type), false);
  if (title && *title)
    ajsono_append(obj, "title", ajson_encode_str(req->pool, title), false);
  ajsona_append(PD(req)->instances, obj);
}

/* Parameters */
void gcloud_v1_embeddings_set_output_dimensionality(curl_event_request_t *req, int dimensions) {
  if (!req || dimensions <= 0) return;
  ajson_t *p = ensure_params(req);
  ajsono_append(p, "outputDimensionality", ajson_number(req->pool, dimensions), false);
}
void gcloud_v1_embeddings_set_auto_truncate(curl_event_request_t *req, bool on) {
  if (!req) return;
  ajson_t *p = ensure_params(req);
  ajsono_append(p, "autoTruncate", on ? ajson_true(req->pool) : ajson_false(req->pool), false);
}
```

---

### `include/a-curl-gcloud-plugin/sinks/v1/embeddings.h`

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H

#include "a-curl-library/curl_event_request.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gcloud_v1_embeddings_complete_cb)(
    void *arg,
    curl_event_request_t *request,
    bool   success,
    float **embeddings,         /* NULL on failure */
    size_t  num_embeddings,
    size_t  embedding_size      /* 0 if unknown/failure */
);

/* Optional stats (predictions[*].embeddings.statistics) */
typedef struct {
  int  token_count;
  int  truncated; /* 0/1 */
} gcloud_v1_embed_stats_t;

typedef void (*gcloud_v1_embeddings_complete_with_stats_cb)(
    void *arg,
    curl_event_request_t *request,
    bool   success,
    float **embeddings,
    size_t  num_embeddings,
    size_t  embedding_size,
    const gcloud_v1_embed_stats_t *stats /* array len == num_embeddings (NULL on failure) */
);

/* Basic sink (values only) */
curl_sink_interface_t *
gcloud_v1_embeddings_sink(curl_event_request_t *req,
                          size_t expected_dim,
                          gcloud_v1_embeddings_complete_cb cb,
                          void *cb_arg);

/* Sink with per-instance statistics */
curl_sink_interface_t *
gcloud_v1_embeddings_sink_with_stats(curl_event_request_t *req,
                                     size_t expected_dim,
                                     gcloud_v1_embeddings_complete_with_stats_cb cb,
                                     void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif /* A_CURL_GCLOUD_PLUGIN_SINK_V1_EMBEDDINGS_H */
```

---

### `src/sinks/v1/embeddings.c`

```c
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// SPDX-FileComment: Independent library for Google Cloud APIs. Not affiliated with Google.

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
```

---

## Quick usage examples

**Gemini (single instance; autoTruncate=false; stats):**

```c
curl_event_request_t *req =
  gcloud_v1_embeddings_init_region(loop, token_res, "my-proj", "us-central1", "gemini-embedding-001");
gcloud_v1_embeddings_set_auto_truncate(req, false);
gcloud_v1_embeddings_add_query(req, "What is the capital of Japan?");

gcloud_v1_embeddings_sink_with_stats(req, /*expected_dim=*/0, on_done_with_stats, ctx);
gcloud_v1_embeddings_submit(loop, req, 0);
```

**Retrieval corpus (documents with titles; 768-dim):**

```c
curl_event_request_t *req =
  gcloud_v1_embeddings_init_region(loop, token_res, "my-proj", "us-central1", "text-embedding-005");
gcloud_v1_embeddings_set_output_dimensionality(req, 768);
gcloud_v1_embeddings_add_document(req, "Some doc content...", "Doc A");
gcloud_v1_embeddings_add_document(req, "More text...",        "Doc B");

gcloud_v1_embeddings_sink(req, 768, on_done, ctx);
gcloud_v1_embeddings_submit(loop, req, 0);
```

---

### Why this covers the doc

* **Instances shape** with `content`, `task_type`, `title` ✅
* **Parameters** `autoTruncate`, `outputDimensionality` ✅
* **Region host + path** ✅
* **Gemini single-instance rule** ✅
* **Response** `predictions[*].embeddings.values` (+ optional `statistics`) ✅

If you want me to run the same pass on your **Pub/Sub, GCS, Vision** pieces for polish/consistency with the OpenAI plugin style (headers, `on_prepare`, dedicated sinks), say the word and I’ll line them up the same way.
