// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/pubsub_pull.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-gcloud-plugin/plugins/token.h" 
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PUBSUB_PULL_URL_FORMAT "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:pull"
#define PUBSUB_ACK_URL_FORMAT  "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:acknowledge"
#define PUBSUB_SEEK_URL_FORMAT "https://pubsub.googleapis.com/v1/projects/%s/subscriptions/%s:seek"

typedef struct {
    curl_sink_interface_t *output;
    curl_event_res_id token_id;
    char *url;
    char *body;
} pubsub_pull_ctx_t;

static void pubsub_output_destroy(void *userdata) {
    pubsub_pull_ctx_t *ctx = (pubsub_pull_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) ctx->output->destroy(ctx->output);
    if (ctx->url) aml_free(ctx->url);
    if (ctx->body) aml_free(ctx->body);
    aml_free(ctx);
}

static bool pubsub_on_prepare(curl_event_request_t *req) {
    pubsub_pull_ctx_t *ctx = (pubsub_pull_ctx_t *)req->plugin_data;
    const gcloud_token_payload_t *p =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!p || !p->access_token) return false;

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", p->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);
    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

static size_t pubsub_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    pubsub_pull_ctx_t *ctx = (pubsub_pull_ctx_t *)req->plugin_data;
    if (ctx && ctx->output) {
        if (!req->sink_initialized && ctx->output->init) {
            ctx->output->init(ctx->output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (ctx->output->write) return ctx->output->write(data, size, nmemb, ctx->output);
    }
    return size * nmemb;
}

static int pubsub_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    pubsub_pull_ctx_t *ctx = (pubsub_pull_ctx_t *)req->plugin_data;
    if (ctx && ctx->output) {
        if (!req->sink_initialized && ctx->output->init) {
            ctx->output->init(ctx->output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (ctx->output->complete) ctx->output->complete(ctx->output, req);
    }
    return 0;
}

static int pubsub_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    pubsub_pull_ctx_t *ctx = (pubsub_pull_ctx_t *)req->plugin_data;
    if (ctx && ctx->output) {
        if (!req->sink_initialized && ctx->output->init) {
            ctx->output->init(ctx->output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (ctx->output->failure) ctx->output->failure(result, http_code, ctx->output, req);
    }
    return -1;
}

static const char *normalize_sub_id(const char *subscription_id) {
    const char *rp = strrchr(subscription_id, '/');
    return rp ? rp + 1 : subscription_id;
}

bool curl_event_plugin_pubsub_pull_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    int max_messages,
    curl_sink_interface_t *output_interface
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || !output_interface) return false;

    pubsub_pull_ctx_t *ctx = aml_calloc(1, sizeof(*ctx));
    ctx->output = output_interface;
    ctx->token_id = token_id;
    ctx->url = aml_strdupf(PUBSUB_PULL_URL_FORMAT, project_id, normalize_sub_id(subscription_id));
    ctx->body = aml_strdupf("{\"maxMessages\":%d,\"returnImmediately\":false}", max_messages);

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->body);

    req->write_cb    = pubsub_on_write;
    req->on_prepare  = pubsub_on_prepare;
    req->on_complete = pubsub_on_complete;
    req->on_failure  = pubsub_on_failure;

    curl_event_request_plugin_data(req, ctx, pubsub_output_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 3;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        pubsub_output_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}

bool curl_event_plugin_pubsub_ack_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char **ack_ids,
    size_t num_ack_ids
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || !ack_ids || num_ack_ids == 0) return false;

    pubsub_pull_ctx_t *ctx = aml_calloc(1, sizeof(*ctx));
    ctx->token_id = token_id;
    ctx->url = aml_strdupf(PUBSUB_ACK_URL_FORMAT, project_id, normalize_sub_id(subscription_id));

    size_t body_cap = 16; 
    for (size_t i = 0; i < num_ack_ids; i++) body_cap += strlen(ack_ids[i]) + 3;
    ctx->body = (char *)aml_calloc(1, body_cap + 16);
    
    char *p = ctx->body;
    p += sprintf(p, "{\"ackIds\":[");
    for (size_t i = 0; i < num_ack_ids; i++) p += sprintf(p, "\"%s\",", ack_ids[i]);
    if (num_ack_ids > 0) p[-1] = ']'; else *p++ = ']';
    *p++ = '}'; *p = '\0';

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->body);

    req->write_cb    = pubsub_on_write;
    req->on_prepare  = pubsub_on_prepare;
    req->on_complete = pubsub_on_complete;
    req->on_failure  = pubsub_on_failure;

    curl_event_request_plugin_data(req, ctx, pubsub_output_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 3;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        pubsub_output_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}

static bool curl_event_plugin_pubsub_seek_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *timestamp,  
    const char *snapshot    
) {
    if (!loop || !project_id || !subscription_id || token_id == 0 || (!timestamp && !snapshot)) return false;

    pubsub_pull_ctx_t *ctx = aml_calloc(1, sizeof(*ctx));
    ctx->token_id = token_id;
    ctx->url = aml_strdupf(PUBSUB_SEEK_URL_FORMAT, project_id, normalize_sub_id(subscription_id));

    if (timestamp)       ctx->body = aml_strdupf("{\"time\":\"%s\"}", timestamp);
    else if (snapshot)   ctx->body = aml_strdupf("{\"snapshot\":\"%s\"}", snapshot);

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->body);

    req->write_cb    = pubsub_on_write;
    req->on_prepare  = pubsub_on_prepare;
    req->on_complete = pubsub_on_complete;
    req->on_failure  = pubsub_on_failure;

    curl_event_request_plugin_data(req, ctx, pubsub_output_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 3;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        pubsub_output_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}

bool curl_event_plugin_pubsub_seek_to_timestamp_init(curl_event_loop_t *loop, const char *project_id, const char *subscription_id, curl_event_res_id  token_id, const char *timestamp) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_id, timestamp, NULL);
}

bool curl_event_plugin_pubsub_seek_to_snapshot_init(curl_event_loop_t *loop, const char *project_id, const char *subscription_id, curl_event_res_id  token_id, const char *snapshot) {
    return curl_event_plugin_pubsub_seek_init(loop, project_id, subscription_id, token_id, NULL, snapshot);
}
