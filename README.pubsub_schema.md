Absolutely—two parts below:

1. **Schema endpoints** for your Pub/Sub plugin (same style as your Topics/Subscriptions HTTP layer).
2. **StreamingPull over gRPC in C** — yes it’s doable; here’s how, plus two practical paths (pure‑C via third‑party vs. C wrapper over the official C++ stubs), with a small starter wrapper.

---

## 1) Pub/Sub **Schema** endpoints (REST/JSON)

This adds full coverage for:

* `schemas.create` (POST `/v1/{parent}/schemas?schemaId=`)
* `schemas.get` (GET `/v1/{name}`)
* `schemas.list` (GET `/v1/{parent}/schemas`)
* `schemas.delete` (DELETE `/v1/{name}`)
* `schemas.commit` (POST `/v1/{name}:commit`)
* `schemas.rollback` (POST `/v1/{name}:rollback`)
* `schemas.deleteRevision` (DELETE `/v1/{name}:deleteRevision?revisionId=`)
* `schemas.listRevisions` (GET `/v1/{name}:listRevisions`)
* `schemas.validate` (POST `/v1/{parent}/schemas:validate`)
* `schemas.validateMessage` (POST `/v1/{parent}/schemas:validateMessage`)

It mirrors the conventions you already have:

* Auth via your token resource
* Base endpoint **global / locational / regional** (same helpers)
* URL‑encoding for **path segments & query params**
* Small, composable setters for request bodies
* Sinks for common response patterns (single schema, list/paging, empty/boolean)

> **Types & encodings**
> • Schema types: `"PROTOCOL_BUFFER"` or `"AVRO"`
> • ValidateMessage encodings: `"BINARY"` or `"JSON"` (message is a **bytes** field in JSON → base64)

---

### A) Headers — **plugin**

**`include/a-curl-gcloud-plugin/plugins/v1/pubsub_schema.h`**

```c
#ifndef A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_SCHEMA_H
#define A_CURL_GCLOUD_PLUGIN_V1_PUBSUB_SCHEMA_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_pool.h"
#include "a-json-library/ajson.h"
#include "a-curl-gcloud-plugin/plugins/v1/pubsub.h"  /* for endpoint helpers + URL-encode */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER = 1,
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO            = 2
} gcloud_v1_pubsub_schema_type_t;

typedef enum {
  GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_BINARY = 1,
  GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_JSON   = 2
} gcloud_v1_pubsub_message_encoding_t;

/* ---------- Create: POST /v1/projects/{project}/schemas?schemaId= ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_create_init(curl_event_loop_t *loop,
                                     curl_event_res_id  token_id,
                                     const char        *base_endpoint,
                                     const char        *project_id,
                                     const char        *schema_id);
/* Body setters (root is the Schema resource: {type, definition}) */
void gcloud_v1_pubsub_schemas_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_set_definition(curl_event_request_t *req, const char *definition);

/* ---------- Get/Delete/List ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_get_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                  const char *base_endpoint, const char *project_id, const char *schema_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                     const char *base_endpoint, const char *project_id, const char *schema_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                   const char *base_endpoint, const char *project_id);
void gcloud_v1_pubsub_schemas_list_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_schemas_list_set_page_token(curl_event_request_t *req, const char *page_token);

/* ---------- Revisions: commit/rollback/delete/list ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_commit_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                     const char *base_endpoint, const char *project_id, const char *schema_id);
/* Commit body has nested "schema": {type, definition} */
void gcloud_v1_pubsub_schemas_commit_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_commit_set_definition(curl_event_request_t *req, const char *definition);

curl_event_request_t *
gcloud_v1_pubsub_schemas_rollback_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                       const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_rollback_set_revision_id(curl_event_request_t *req, const char *revision_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_revision_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                              const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_delete_revision_set_revision_id(curl_event_request_t *req, const char *revision_id);

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_revisions_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                             const char *base_endpoint, const char *project_id, const char *schema_id);
void gcloud_v1_pubsub_schemas_list_revisions_set_page_size(curl_event_request_t *req, int page_size);
void gcloud_v1_pubsub_schemas_list_revisions_set_page_token(curl_event_request_t *req, const char *page_token);

/* ---------- Validate & ValidateMessage ---------- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                       const char *base_endpoint, const char *project_id);
/* body: { schema: { type, definition } } */
void gcloud_v1_pubsub_schemas_validate_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t type);
void gcloud_v1_pubsub_schemas_validate_set_definition(curl_event_request_t *req, const char *definition);

curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_message_init(curl_event_loop_t *loop, curl_event_res_id token_id,
                                               const char *base_endpoint, const char *project_id);
/* choose schema by name OR inline */
void gcloud_v1_pubsub_schemas_validate_message_use_schema_name(curl_event_request_t *req, const char *full_name);
void gcloud_v1_pubsub_schemas_validate_message_set_inline(curl_event_request_t *req,
                                                          gcloud_v1_pubsub_schema_type_t type,
                                                          const char *definition);
/* message and encoding */
void gcloud_v1_pubsub_schemas_validate_message_set_encoding(curl_event_request_t *req,
                                                            gcloud_v1_pubsub_message_encoding_t encoding);
void gcloud_v1_pubsub_schemas_validate_message_set_bytes(curl_event_request_t *req,
                                                         const void *data, size_t len);
void gcloud_v1_pubsub_schemas_validate_message_set_json(curl_event_request_t *req,
                                                        const char *json_string);

#ifdef __cplusplus
}
#endif
#endif
```

---

### B) Headers — **sinks**

**`include/a-curl-gcloud-plugin/sinks/v1/pubsub_schema.h`**

```c
#ifndef A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_SCHEMA_H
#define A_CURL_GCLOUD_PLUGIN_SINK_V1_PUBSUB_SCHEMA_H

#include "a-curl-library/curl_event_request.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER = 1,
  GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO            = 2
} gcloud_v1_pubsub_schema_type_t;

typedef struct {
  const char *name;                  /* projects/{p}/schemas/{id}[@{rev}] */
  gcloud_v1_pubsub_schema_type_t type;
  const char *definition;            /* may be present */
  const char *revision_id;           /* may be present */
  const char *revision_create_time;  /* RFC3339, may be present */
} gcloud_v1_pubsub_schema_t;

/* Single Schema resource result (create/get/commit/rollback) */
typedef void (*gcloud_v1_pubsub_schema_cb)(
  void *arg, curl_event_request_t *request, bool success,
  const gcloud_v1_pubsub_schema_t *schema /* pool-owned, nullable on failure */
);
curl_sink_interface_t *
gcloud_v1_pubsub_schema_sink(curl_event_request_t *req,
                             gcloud_v1_pubsub_schema_cb cb, void *cb_arg);

/* List schemas (names + page token) */
typedef void (*gcloud_v1_pubsub_schemas_list_cb)(
  void *arg, curl_event_request_t *request, bool success,
  const gcloud_v1_pubsub_schema_t *schemas, size_t count,
  const char *next_page_token /* nullable */
);
curl_sink_interface_t *
gcloud_v1_pubsub_schemas_list_sink(curl_event_request_t *req,
                                   gcloud_v1_pubsub_schemas_list_cb cb, void *cb_arg);

/* Empty/boolean (delete, deleteRevision, validate, validateMessage) */
typedef void (*gcloud_v1_pubsub_empty_cb)(
  void *arg, curl_event_request_t *request, bool success
);
curl_sink_interface_t *
gcloud_v1_pubsub_empty_sink(curl_event_request_t *req,
                            gcloud_v1_pubsub_empty_cb cb, void *cb_arg);

#ifdef __cplusplus
}
#endif
#endif
```

---

### C) Implementations

Below are compact implementations (mirroring your existing Pub/Sub file). They reuse the same **on\_prepare**, retry, and **URL‑encode** logic you already have. For brevity, I’m including focused excerpts — you can drop these into:

* `src/plugins/v1/pubsub_schema.c`
* `src/sinks/v1/pubsub_schema.c`

#### `src/plugins/v1/pubsub_schema.c` (selected)

```c
#include "a-curl-gcloud-plugin/plugins/v1/pubsub_schema.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

/* ---- shared request scaffold (same pattern as pubsub.c) ---- */
typedef struct {
  curl_event_res_id token_id;
  const char *base_endpoint;
  ajson_t *root;
  int page_size;
  const char *page_token;
} schema_pd_t;

static bool _on_prepare(curl_event_request_t *req) {
  schema_pd_t *pd = (schema_pd_t *)req->plugin_data;
  const gcloud_token_payload_t *tok =
    (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, pd->token_id);
  if (!tok || !tok->access_token) return false;
  char auth[1024]; snprintf(auth, sizeof auth, "Bearer %s", tok->access_token);
  curl_event_request_set_header(req, "Authorization", auth);
  curl_event_request_set_header(req, "Content-Type", "application/json");
  curl_event_request_set_header(req, "Accept", "application/json");
  if (pd->root) curl_event_request_json_commit(req);
  return true;
}
static curl_event_request_t *
_start(curl_event_loop_t *loop, curl_event_res_id token_id,
       const char *base, const char *method, const char *path, bool want_json) {
  curl_event_request_t *req = curl_event_request_init(0);
  char *url = aml_pool_alloc(req->pool, strlen(base) + strlen(path) + 1);
  sprintf(url, "%s%s", base, path);
  curl_event_request_url(req, url);
  curl_event_request_method(req, method);
  schema_pd_t *pd = aml_pool_calloc(req->pool, 1, sizeof *pd);
  pd->token_id = token_id; pd->base_endpoint = aml_pool_strdup(req->pool, base);
  if (want_json) pd->root = curl_event_request_json_begin(req, false);
  curl_event_request_plugin_data(req, pd, NULL);
  curl_event_request_depend(req, token_id);
  curl_event_request_on_prepare(req, _on_prepare);
  curl_event_request_enable_retries(req, 3, 2.0, 250, 20000, true);
  return req;
}
static void _apply_paging(curl_event_request_t *req) {
  schema_pd_t *pd = (schema_pd_t *)req->plugin_data;
  const char *old = curl_event_request_get_url(req);
  const char *q = strchr(old, '?') ? "&" : "?";
  size_t need = 64 + (pd->page_token ? strlen(pd->page_token) : 0);
  char *url = aml_pool_alloc(req->pool, strlen(old) + need);
  size_t pos = sprintf(url, "%s", old);
  if (pd->page_size > 0) pos += sprintf(url+pos, "%spageSize=%d", q, pd->page_size), q="&";
  if (pd->page_token && *pd->page_token) pos += sprintf(url+pos, "%spageToken=%s", q, pd->page_token);
  curl_event_request_url(req, url);
}

/* ---- helpers ---- */
static const char *_type_str(gcloud_v1_pubsub_schema_type_t t) {
  return (t == GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO) ? "AVRO" : "PROTOCOL_BUFFER";
}
static const char *_enc_str(gcloud_v1_pubsub_message_encoding_t e) {
  return (e == GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_JSON) ? "JSON" : "BINARY";
}
/* simple b64 (reuse from pubsub.c if you prefer) */
static const char B64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64enc(aml_pool_t *pool, const void *data, size_t len) {
  const unsigned char *in = data; size_t out_len = ((len+2)/3)*4;
  char *out = aml_pool_alloc(pool, out_len+1); char *p = out;
  for (size_t i=0;i<len;i+=3) {
    unsigned v = in[i]<<16; if (i+1<len) v|=in[i+1]<<8; if (i+2<len) v|=in[i+2];
    *p++=B64[(v>>18)&63]; *p++=B64[(v>>12)&63];
    *p++=(i+1<len)?B64[(v>>6)&63]:'='; *p++=(i+2<len)?B64[v&63]:'=';
  } *p=0; return out;
}

/* ---- create ---- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_create_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id)
{
  aml_pool_t *tmp = aml_pool_init();
  const char *sid = gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024];
  snprintf(path, sizeof path, "/v1/projects/%s/schemas?schemaId=%s", project, sid);
  curl_event_request_t *req = _start(loop, tok, base, "POST", path, true);
  aml_pool_destroy(tmp);
  return req;
}
void gcloud_v1_pubsub_schemas_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t t) {
  ajsono_append(((schema_pd_t*)req->plugin_data)->root, "type", ajson_encode_str(req->pool, _type_str(t)), false);
}
void gcloud_v1_pubsub_schemas_set_definition(curl_event_request_t *req, const char *def) {
  ajsono_append(((schema_pd_t*)req->plugin_data)->root, "definition", ajson_encode_str(req->pool, def), false);
}

/* ---- get/delete/list ---- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_get_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                  const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"GET",path,false); aml_pool_destroy(tmp); return req;
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"DELETE",path,false); aml_pool_destroy(tmp); return req;
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_list_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                   const char *base, const char *project) {
  char path[512]; snprintf(path,sizeof path,"/v1/projects/%s/schemas", project);
  return _start(loop,tok,base,"GET",path,false);
}
void gcloud_v1_pubsub_schemas_list_set_page_size(curl_event_request_t *req, int n) {
  ((schema_pd_t*)req->plugin_data)->page_size = n; _apply_paging(req);
}
void gcloud_v1_pubsub_schemas_list_set_page_token(curl_event_request_t *req, const char *tok) {
  ((schema_pd_t*)req->plugin_data)->page_token = aml_pool_strdup(req->pool, tok?tok:""); _apply_paging(req);
}

/* ---- commit (nested schema) ---- */
static ajson_t *_ensure_nested_schema(curl_event_request_t *req) {
  schema_pd_t *pd = (schema_pd_t*)req->plugin_data;
  ajson_t *s = ajsono_scan(pd->root, "schema");
  if (!s) { s = ajsono(req->pool); ajsono_append(pd->root,"schema",s,false); }
  return s;
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_commit_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:commit", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"POST",path,true); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_commit_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t t) {
  ajsono_append(_ensure_nested_schema(req),"type", ajson_encode_str(req->pool,_type_str(t)), false);
}
void gcloud_v1_pubsub_schemas_commit_set_definition(curl_event_request_t *req, const char *def) {
  ajsono_append(_ensure_nested_schema(req),"definition", ajson_encode_str(req->pool,def), false);
}

/* ---- rollback/deleteRevision/listRevisions ---- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_rollback_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                       const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:rollback", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"POST",path,true); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_rollback_set_revision_id(curl_event_request_t *req, const char *rev) {
  ajsono_append(((schema_pd_t*)req->plugin_data)->root,"revisionId", ajson_encode_str(req->pool,rev), false);
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_revision_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                              const char *base, const char *project, const char *schema_id) {
  /* revisionId is added later as query param */
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:deleteRevision", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"DELETE",path,false);
  aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_delete_revision_set_revision_id(curl_event_request_t *req, const char *rev) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  aml_pool_t *tmp=aml_pool_init(); const char *q=gcloud_v1_pubsub_url_encode_segment(tmp, rev);
  const char *old=curl_event_request_get_url(req);
  char *url=aml_pool_alloc(req->pool, strlen(old)+20+strlen(q));
  sprintf(url,"%s?revisionId=%s",old,q);
  curl_event_request_url(req,url);
  aml_pool_destroy(tmp);
}

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_revisions_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                             const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:listRevisions", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"GET",path,false); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_list_revisions_set_page_size(curl_event_request_t *req, int n) {
  ((schema_pd_t*)req->plugin_data)->page_size = n; _apply_paging(req);
}
void gcloud_v1_pubsub_schemas_list_revisions_set_page_token(curl_event_request_t *req, const char *tok) {
  ((schema_pd_t*)req->plugin_data)->page_token = aml_pool_strdup(req->pool, tok?tok:""); _apply_paging(req);
}

/* ---- validate & validateMessage ---- */
curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                       const char *base, const char *project) {
  char path[512]; snprintf(path,sizeof path,"/v1/projects/%s/schemas:validate", project);
  return _start(loop,tok,base,"POST",path,true);
}
void gcloud_v1_pubsub_schemas_validate_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t t) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  ajson_t *schema = ajsono_scan(pd->root,"schema"); if (!schema){schema=ajsono(req->pool); ajsono_append(pd->root,"schema",schema,false);}
  ajsono_append(schema,"type", ajson_encode_str(req->pool,_type_str(t)), false);
}
void gcloud_v1_pubsub_schemas_validate_set_definition(curl_event_request_t *req, const char *def) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  ajson_t *schema = ajsono_scan(pd->root,"schema"); if (!schema){schema=ajsono(req->pool); ajsono_append(pd->root,"schema",schema,false);}
  ajsono_append(schema,"definition", ajson_encode_str(req->pool,def), false);
}

curl_event_request_t *
gcloud_v1_pubsub_schemas_validate_message_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                               const char *base, const char *project) {
  char path[512]; snprintf(path,sizeof path,"/v1/projects/%s/schemas:validateMessage", project);
  return _start(loop,tok,base,"POST",path,true);
}
void gcloud_v1_pubsub_schemas_validate_message_use_schema_name(curl_event_request_t *req, const char *full) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  ajson_t *schema = ajsono(req->pool);
  ajsono_append(schema,"name", ajson_encode_str(req->pool, full), false);
  ajsono_append(pd->root,"schema", schema, true);
}
void gcloud_v1_pubsub_schemas_validate_message_set_inline(curl_event_request_t *req,
                                                          gcloud_v1_pubsub_schema_type_t t,
                                                          const char *def) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  ajson_t *schema = ajsono(req->pool);
  ajsono_append(schema,"type", ajson_encode_str(req->pool,_type_str(t)), false);
  ajsono_append(schema,"definition", ajson_encode_str(req->pool,def), false);
  ajsono_append(pd->root,"schema", schema, true);
}
void gcloud_v1_pubsub_schemas_validate_message_set_encoding(curl_event_request_t *req,
                                                            gcloud_v1_pubsub_message_encoding_t e) {
  ajsono_append(((schema_pd_t*)req->plugin_data)->root,"encoding",
                ajson_encode_str(req->pool,_enc_str(e)), true);
}
void gcloud_v1_pubsub_schemas_validate_message_set_bytes(curl_event_request_t *req,
                                                         const void *data, size_t len) {
  char *b64 = b64enc(req->pool, data, len);
  ajsono_append(((schema_pd_t*)req->plugin_data)->root,"message",
                ajson_encode_str(req->pool, b64), true);
}
void gcloud_v1_pubsub_schemas_validate_message_set_json(curl_event_request_t *req,
                                                        const char *json) {
  gcloud_v1_pubsub_schemas_validate_message_set_bytes(req, json, strlen(json));
}
```

#### `src/sinks/v1/pubsub_schema.c` (selected)

```c
#include "a-curl-gcloud-plugin/sinks/v1/pubsub_schema.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include <string.h>
#include <stdio.h>

typedef struct { curl_sink_interface_t iface; aml_buffer_t *resp;
                 void *cb; void *arg; const char *array_field; } base_t;

static bool s_init(curl_sink_interface_t *i,long){ base_t*s=(void*)i; s->resp=aml_buffer_init(2048); return s->resp!=NULL; }
static size_t s_write(const void *p,size_t s,size_t n,curl_sink_interface_t *i){ base_t*b=(void*)i; aml_buffer_append(b->resp,p,s*n); return s*n; }
static void s_destroy(curl_sink_interface_t *i){ base_t*s=(void*)i; if (s->resp) aml_buffer_destroy(s->resp); }

/* map type string -> enum */
static gcloud_v1_pubsub_schema_type_t map_type(const char *t) {
  if (t && strcmp(t,"AVRO")==0) return GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO;
  return GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER;
}

/* ---- single schema sink ---- */
typedef struct { base_t b; gcloud_v1_pubsub_schema_cb cb; } sink_schema_t;
static void schema_fail(CURLcode rc,long http,curl_sink_interface_t *i,curl_event_request_t *r){
  sink_schema_t*s=(void*)i; if(s->cb) s->cb(s->b.arg,r,false,NULL);
}
static void schema_done(curl_sink_interface_t *i,curl_event_request_t *r){
  sink_schema_t*s=(void*)i; aml_pool_t *pool=i->pool;
  ajson_t *j=ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if(!j||ajson_is_error(j)){ if(s->cb) s->cb(s->b.arg,r,false,NULL); return; }
  gcloud_v1_pubsub_schema_t *out = aml_pool_zalloc(pool, sizeof *out);
  out->name  = ajsono_scan_str(j,"name",NULL);
  out->definition = ajsono_scan_str(j,"definition",NULL);
  out->revision_id = ajsono_scan_str(j,"revisionId",NULL);
  out->revision_create_time = ajsono_scan_str(j,"revisionCreateTime",NULL);
  out->type = map_type(ajsono_scan_str(j,"type",NULL));
  if(s->cb) s->cb(s->b.arg,r,true,out);
}
curl_sink_interface_t *
gcloud_v1_pubsub_schema_sink(curl_event_request_t *req,
                             gcloud_v1_pubsub_schema_cb cb, void *arg)
{
  sink_schema_t *s = aml_pool_zalloc(req->pool, sizeof *s);
  s->cb=cb; s->b.arg=arg; s->b.iface.pool=req->pool;
  s->b.iface.init=s_init; s->b.iface.write=s_write; s->b.iface.destroy=s_destroy;
  s->b.iface.failure=schema_fail; s->b.iface.complete=schema_done;
  curl_event_request_sink(req,(curl_sink_interface_t*)s,NULL);
  return (curl_sink_interface_t*)s;
}

/* ---- list sink (schemas or revisions) ---- */
typedef struct { base_t b; gcloud_v1_pubsub_schemas_list_cb cb; } sink_list_t;
static void list_fail(CURLcode rc,long http,curl_sink_interface_t *i,curl_event_request_t *r){
  sink_list_t*s=(void*)i; if(s->cb) s->cb(s->b.arg,r,false,NULL,0,NULL);
}
static void list_done(curl_sink_interface_t *i,curl_event_request_t *r){
  sink_list_t*s=(void*)i; aml_pool_t *pool=i->pool;
  ajson_t *j=ajson_parse_string(pool, aml_buffer_data(s->b.resp));
  if(!j||ajson_is_error(j)){ if(s->cb) s->cb(s->b.arg,r,false,NULL,0,NULL); return; }
  ajson_t *arr = ajsono_scan(j, s->b.array_field ? s->b.array_field : "schemas");
  size_t n = arr && ajson_is_array(arr) ? ajsona_count(arr) : 0;
  gcloud_v1_pubsub_schema_t *out = n ? aml_pool_calloc(pool,n,sizeof *out) : NULL;
  size_t k=0;
  for (ajsona_t *el = ajsona_first(arr); el; el = ajsona_next(el), ++k) {
    ajson_t *o = el->value;
    out[k].name = ajsono_scan_str(o,"name",NULL);
    out[k].definition = ajsono_scan_str(o,"definition",NULL); /* might be omitted */
    out[k].revision_id = ajsono_scan_str(o,"revisionId",NULL);
    out[k].revision_create_time = ajsono_scan_str(o,"revisionCreateTime",NULL);
    out[k].type = map_type(ajsono_scan_str(o,"type",NULL));
  }
  const char *next = ajsono_scan_str(j,"nextPageToken",NULL);
  if(s->cb) s->cb(s->b.arg,r,true,out,n,next);
}
curl_sink_interface_t *
gcloud_v1_pubsub_schemas_list_sink(curl_event_request_t *req,
                                   gcloud_v1_pubsub_schemas_list_cb cb, void *arg) {
  sink_list_t *s=aml_pool_zalloc(req->pool,sizeof *s);
  s->cb=cb; s->b.arg=arg; s->b.iface.pool=req->pool;
  s->b.iface.init=s_init; s->b.iface.write=s_write; s->b.iface.destroy=s_destroy;
  s->b.iface.failure=list_fail; s->b.iface.complete=list_done;
  curl_event_request_sink(req,(curl_sink_interface_t*)s,NULL);
  return (curl_sink_interface_t*)s;
}

/* ---- empty sink (delete, deleteRevision, validate, validateMessage) ---- */
typedef struct { base_t b; gcloud_v1_pubsub_empty_cb cb; } sink_empty_t;
static void empty_fail(CURLcode rc,long http,curl_sink_interface_t *i,curl_event_request_t *r){
  sink_empty_t*s=(void*)i; if(s->cb) s->cb(s->b.arg,r,false);
}
static void empty_done(curl_sink_interface_t *i,curl_event_request_t *r){
  sink_empty_t*s=(void*)i; if(s->cb) s->cb(s->b.arg,r,true);
}
curl_sink_interface_t *
gcloud_v1_pubsub_empty_sink(curl_event_request_t *req,
                            gcloud_v1_pubsub_empty_cb cb, void *arg) {
  sink_empty_t *s=aml_pool_zalloc(req->pool,sizeof *s);
  s->cb=cb; s->b.arg=arg; s->b.iface.pool=req->pool;
  s->b.iface.init=s_init; s->b.iface.write=s_write; s->b.iface.destroy=s_destroy;
  s->b.iface.failure=empty_fail; s->b.iface.complete=empty_done;
  curl_event_request_sink(req,(curl_sink_interface_t*)s,NULL);
  return (curl_sink_interface_t*)s;
}
```

---

### D) Usage snippets

```c
/* Choose endpoint (global / locational / regional) */
const char *BASE = gcloud_v1_pubsub_endpoint_global();

/* 1) Create an AVRO schema */
curl_event_request_t *cs =
  gcloud_v1_pubsub_schemas_create_init(loop, token_res, BASE, "my-proj", "user-events");
gcloud_v1_pubsub_schemas_set_type(cs, GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO);
gcloud_v1_pubsub_schemas_set_definition(cs,
  "{ \"type\":\"record\", \"name\":\"UserEvent\", \"fields\":["
  "{ \"name\":\"user_id\",\"type\":\"string\"},"
  "{ \"name\":\"event_type\",\"type\":\"string\"}]}");
gcloud_v1_pubsub_schema_sink(cs, on_schema, ctx);
gcloud_v1_pubsub_submit(loop, cs, 0);

/* 2) Validate a JSON message against the schema by name */
curl_event_request_t *vm =
  gcloud_v1_pubsub_schemas_validate_message_init(loop, token_res, BASE, "my-proj");
gcloud_v1_pubsub_schemas_validate_message_use_schema_name(vm,
  "projects/my-proj/schemas/user-events");
gcloud_v1_pubsub_schemas_validate_message_set_encoding(vm,
  GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_JSON);
gcloud_v1_pubsub_schemas_validate_message_set_json(vm,
  "{ \"user_id\": \"u-1\", \"event_type\": \"login\" }");
gcloud_v1_pubsub_empty_sink(vm, on_validated, ctx);
gcloud_v1_pubsub_submit(loop, vm, 0);

/* 3) Commit a new PROTOBUF revision */
curl_event_request_t *com =
  gcloud_v1_pubsub_schemas_commit_init(loop, token_res, BASE, "my-proj", "user-events");
gcloud_v1_pubsub_schemas_commit_set_type(com, GCLOUD_V1_PUBSUB_SCHEMA_TYPE_PROTOCOL_BUFFER);
gcloud_v1_pubsub_schemas_commit_set_definition(com,
  "syntax = \"proto3\"; package ue; message UserEvent { string user_id=1; string event_type=2; }");
gcloud_v1_pubsub_schema_sink(com, on_schema, ctx);
gcloud_v1_pubsub_submit(loop, com, 0);

/* 4) List revisions */
curl_event_request_t *lr =
  gcloud_v1_pubsub_schemas_list_revisions_init(loop, token_res, BASE, "my-proj", "user-events");
gcloud_v1_pubsub_schemas_list_set_page_size(lr, 50);
gcloud_v1_pubsub_schemas_list_sink(lr, on_schema_list, ctx);
gcloud_v1_pubsub_submit(loop, lr, 0);
```

That’s everything you need to wire **Schemas** into your existing plugin.

---

## 2) Can **StreamingPull** work over **gRPC in C**?

**Yes.** Pub/Sub’s `StreamingPull` is a **bidirectional gRPC** method on the Subscriber service. You can absolutely do it in a C codebase, but there are two realistic implementation paths:

### Option A — **Official gRPC C++ stubs** + a tiny **C façade**  ✅ (recommended)

* Use the official `protoc` + `grpc_cpp_plugin` to generate C++ stubs from `google/pubsub/v1/pubsub.proto`.
* Link against `grpc++` and **Google default credentials** (`grpc::GoogleDefaultCredentials()`), which picks up ADC (env vars, gcloud, GCE metadata).
* Write a thin `extern "C"` layer that exposes C functions your code can call (start/stop stream, push acks, etc.). This keeps your codebase “C at the edges” without fighting proto/gRPC codegen availability.
* Works cross‑platform and tracks Google’s supported surface.

**Pros:** Fully supported, best ergonomics & TLS/credentials story.
**Cons:** Needs a C++ build step; you ship one tiny C++ object.

### Option B — **Pure C** with third‑party **grpc‑c + protobuf‑c**  🧪

* Use `protobuf-c` (`protoc-c`) for message types and **grpc-c** for the streaming RPC.
* Entirely C (no C++), but you rely on community tooling. Make sure streaming bidi RPCs are supported in your chosen versions; you’ll also have to handle **TLS & Google call credentials** manually (typically composing channel creds + per‑RPC `authorization` metadata with your bearer token).

**Pros:** Pure C.
**Cons:** More setup, fewer ready-made helpers for Google creds, you own more plumbing.

---

### Minimal shape of a **C façade** over the official C++ stubs

**Header** (C‑side):

```c
/* include/a-grpc-gcloud-plugin/pubsub_stream.h */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct gcloud_pubsub_stream gcloud_pubsub_stream_t;

typedef struct {
  const char *endpoint;    /* "pubsub.googleapis.com:443" or locational host */
  const char *subscription;/* "projects/{p}/subscriptions/{s}" */
  int stream_ack_deadline_seconds; /* e.g., 60 */
  int max_outstanding_messages;    /* optional flow control */
  long max_outstanding_bytes;      /* optional flow control */
  int exactly_once;                /* boolean */
} gcloud_pubsub_stream_opts_t;

typedef struct {
  const char *ack_id;
  const char *message_id;
  const char *publish_time;      /* RFC3339 */
  const char *ordering_key;      /* nullable */
  const uint8_t *data; size_t data_len;  /* raw bytes */
  const char **attr_keys; const char **attr_vals; size_t num_attrs;
  int delivery_attempt;
} gcloud_pubsub_stream_msg_t;

typedef void (*gcloud_pubsub_stream_on_recv)(
  void *arg, const gcloud_pubsub_stream_msg_t *msgs, size_t n);

int gcloud_pubsub_stream_start(const gcloud_pubsub_stream_opts_t *opts,
                               gcloud_pubsub_stream_on_recv on_recv,
                               void *arg,
                               gcloud_pubsub_stream_t **out_stream);
/* Send acks over the same stream (preferred) */
int gcloud_pubsub_stream_ack(gcloud_pubsub_stream_t *s,
                             const char **ack_ids, size_t n);
/* Modify deadlines over the stream */
int gcloud_pubsub_stream_modify_deadline(gcloud_pubsub_stream_t *s,
                                         const char **ack_ids, const int *seconds,
                                         size_t n);
/* Shut down the bidi stream and join worker */
void gcloud_pubsub_stream_stop(gcloud_pubsub_stream_t *s);

#ifdef __cplusplus
}
#endif
```

**Implementation sketch** (C++ file exposing C API):

* Build a `grpc::Channel` with `grpc::GoogleDefaultCredentials()` (or Composite creds if you need extra per‑RPC metadata).
* Create `google::pubsub::v1::Subscriber::Stub`.
* Call `Stub::StreamingPull(&context)` to obtain a `std::unique_ptr<grpc::ClientReaderWriter<StreamingPullRequest, StreamingPullResponse>>`.
* Spawn a background thread that:

    * writes the initial `StreamingPullRequest` with `subscription`, `stream_ack_deadline_seconds`, and (optionally) flow control settings,
    * loops on `Read(response)` to deliver `received_messages` to your `on_recv` callback,
    * periodically sends heartbeats (empty requests) if desired.
* `gcloud_pubsub_stream_ack` constructs a `StreamingPullRequest` with `ack_ids` populated and `Write()`s it on the same stream.
* `modify_deadline` similarly fills `modify_deadline_ack_ids` and `modify_deadline_seconds` (parallel arrays) and `Write()`s it.

This lets you preserve your **event‑loop + callbacks** model while getting the **lower latency & higher throughput** of StreamingPull.

> **Why not do StreamingPull over HTTP?**
> It’s gRPC‑only; the JSON/HTTP surface does **not** provide streaming pull. Your `curl` layer already covers Pull/Ack for simpler cases.

---

### Credentials & endpoints (gRPC)

* **Endpoint**: Use the same host patterns you already support:

    * Global: `pubsub.googleapis.com:443`
    * Locational: `{location}-pubsub.googleapis.com:443`
    * Regional: `pubsub.{region}.rep.googleapis.com:443`
* **Credentials**:

    * With C++ stubs: `auto creds = grpc::GoogleDefaultCredentials();`
    * With pure‑C stacks: combine **TLS** + per‑RPC `authorization: Bearer <token>` metadata (your existing OAuth flow can supply tokens; refresh as needed).

---

## Quick “what should I do?” guidance

* If you’re okay adding a tiny C++ object file: **choose Option A** (C façade over official C++ stubs). It’s the least friction and matches Google’s supported path.
* If you must be 100% C: **Option B** works, but expect more glue (codegen, TLS, and auth metadata).

If you want, I can drop in a concrete `pubsub_stream.cc` (C++ wrapper) with the actual `StreamingPullRequest/Response` usage and a minimal CMake snippet to compile only that one file as C++.
