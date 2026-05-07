// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

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
