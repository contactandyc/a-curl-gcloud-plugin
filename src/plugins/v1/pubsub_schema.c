// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/v1/pubsub_schema.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <string.h>

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
  const char *old = req->url;
  const char *q = strchr(old, '?') ? "&" : "?";
  size_t need = 64 + (pd->page_token ? strlen(pd->page_token) : 0);
  char *url = aml_pool_alloc(req->pool, strlen(old) + need);
  size_t pos = sprintf(url, "%s", old);
  if (pd->page_size > 0) pos += sprintf(url+pos, "%spageSize=%d", q, pd->page_size), q="&";
  if (pd->page_token && *pd->page_token) pos += sprintf(url+pos, "%spageToken=%s", q, pd->page_token);
  curl_event_request_url(req, url);
}

static const char *_type_str(gcloud_v1_pubsub_schema_type_t t) {
  return (t == GCLOUD_V1_PUBSUB_SCHEMA_TYPE_AVRO) ? "AVRO" : "PROTOCOL_BUFFER";
}
static const char *_enc_str(gcloud_v1_pubsub_message_encoding_t e) {
  return (e == GCLOUD_V1_PUBSUB_MESSAGE_ENCODING_JSON) ? "JSON" : "BINARY";
}

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

curl_event_request_t *
gcloud_v1_pubsub_schemas_create_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id)
{
  aml_pool_t *tmp = aml_pool_init(1024);
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

curl_event_request_t *
gcloud_v1_pubsub_schemas_get_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                  const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"GET",path,false); aml_pool_destroy(tmp); return req;
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
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

static ajson_t *_ensure_nested_schema(curl_event_request_t *req) {
  schema_pd_t *pd = (schema_pd_t*)req->plugin_data;
  ajson_t *s = ajsono_scan(pd->root, "schema");
  if (!s) { s = ajsono(req->pool); ajsono_append(pd->root,"schema",s,false); }
  return s;
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_commit_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                     const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:commit", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"POST",path,true); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_commit_set_type(curl_event_request_t *req, gcloud_v1_pubsub_schema_type_t t) {
  ajsono_append(_ensure_nested_schema(req),"type", ajson_encode_str(req->pool,_type_str(t)), false);
}
void gcloud_v1_pubsub_schemas_commit_set_definition(curl_event_request_t *req, const char *def) {
  ajsono_append(_ensure_nested_schema(req),"definition", ajson_encode_str(req->pool,def), false);
}

curl_event_request_t *
gcloud_v1_pubsub_schemas_rollback_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                       const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:rollback", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"POST",path,true); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_rollback_set_revision_id(curl_event_request_t *req, const char *rev) {
  ajsono_append(((schema_pd_t*)req->plugin_data)->root,"revisionId", ajson_encode_str(req->pool,rev), false);
}
curl_event_request_t *
gcloud_v1_pubsub_schemas_delete_revision_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                              const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:deleteRevision", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"DELETE",path,false);
  aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_delete_revision_set_revision_id(curl_event_request_t *req, const char *rev) {
  schema_pd_t *pd=(schema_pd_t*)req->plugin_data;
  aml_pool_t *tmp=aml_pool_init(1024); const char *q=gcloud_v1_pubsub_url_encode_segment(tmp, rev);
  const char *old=req->url;
  char *url=aml_pool_alloc(req->pool, strlen(old)+20+strlen(q));
  sprintf(url,"%s?revisionId=%s",old,q);
  curl_event_request_url(req,url);
  aml_pool_destroy(tmp);
}

curl_event_request_t *
gcloud_v1_pubsub_schemas_list_revisions_init(curl_event_loop_t *loop, curl_event_res_id tok,
                                             const char *base, const char *project, const char *schema_id) {
  aml_pool_t *tmp=aml_pool_init(1024); const char *sid=gcloud_v1_pubsub_url_encode_segment(tmp, schema_id);
  char path[1024]; snprintf(path,sizeof path,"/v1/projects/%s/schemas/%s:listRevisions", project, sid);
  curl_event_request_t *req=_start(loop,tok,base,"GET",path,false); aml_pool_destroy(tmp); return req;
}
void gcloud_v1_pubsub_schemas_list_revisions_set_page_size(curl_event_request_t *req, int n) {
  ((schema_pd_t*)req->plugin_data)->page_size = n; _apply_paging(req);
}
void gcloud_v1_pubsub_schemas_list_revisions_set_page_token(curl_event_request_t *req, const char *tok) {
  ((schema_pd_t*)req->plugin_data)->page_token = aml_pool_strdup(req->pool, tok?tok:""); _apply_paging(req);
}

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
