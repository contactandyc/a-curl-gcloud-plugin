// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/cloudsql_query.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *CLOUDSQL_QUERY_URL_FORMAT =
    "https://sqladmin.googleapis.com/v1/projects/%s/instances/%s/databases/%s/executeQuery";

typedef struct cloudsql_ctx_s {
    curl_event_res_id  token_id;
    char              *url;
    char              *post_body;
    curl_sink_interface_t *output;
} cloudsql_ctx_t;

static void cloudsql_ctx_destroy(void *userdata) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) {
        ctx->output->destroy(ctx->output);
    }
    if (ctx->url)      aml_free(ctx->url);
    if (ctx->post_body) aml_free(ctx->post_body);
    aml_free(ctx);
}

static bool cloudsql_on_prepare(curl_event_request_t *req) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->plugin_data;
    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
        
    if (!tok || !tok->access_token) {
        fprintf(stderr, "[cloudsql_query] token not available\n");
        return false;
    }

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);
    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

static size_t cloudsql_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->plugin_data;
    curl_sink_interface_t *out = ctx ? ctx->output : NULL;
    if (out) {
        if (!req->sink_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (out->write) return out->write(data, size, nmemb, out);
    }
    return size * nmemb;
}

static int cloudsql_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->plugin_data;
    curl_sink_interface_t *out = ctx ? ctx->output : NULL;
    if (out) {
        if (!req->sink_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (out->complete) out->complete(out, req);
    }
    return 0;
}

static int cloudsql_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)req->plugin_data;
    curl_sink_interface_t *out = ctx ? ctx->output : NULL;
    if (out) {
        if (!req->sink_initialized && out->init) {
            out->init(out, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (out->failure) out->failure(result, http_code, out, req);
    }
    return 0;
}

bool curl_event_plugin_cloudsql_query_init(
    curl_event_loop_t *loop,
    const char *instance_connection_name,
    const char *database,
    curl_event_res_id  token_id,
    const char *query,
    curl_sink_interface_t *output_interface
) {
    if (!loop || !instance_connection_name || !database || !query || !output_interface || token_id == 0) return false;

    const char *project = "your-project-id"; 
    const char *instance = instance_connection_name;

    char url_buf[1024];
    snprintf(url_buf, sizeof(url_buf), CLOUDSQL_QUERY_URL_FORMAT, project, instance, database);

    char body_buf[2048];
    snprintf(body_buf, sizeof(body_buf), "{ \"query\": \"%s\" }", query);

    cloudsql_ctx_t *ctx = (cloudsql_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) return false;
    
    ctx->token_id  = token_id;
    ctx->url       = aml_strdup(url_buf);
    ctx->post_body = aml_strdup(body_buf);
    ctx->output    = output_interface;

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->post_body);

    req->write_cb    = cloudsql_on_write;
    req->on_prepare  = cloudsql_on_prepare;
    req->on_complete = cloudsql_on_complete;
    req->on_failure  = cloudsql_on_failure;

    curl_event_request_plugin_data(req, ctx, cloudsql_ctx_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 5;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        cloudsql_ctx_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}
