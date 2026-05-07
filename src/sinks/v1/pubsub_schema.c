// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

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
