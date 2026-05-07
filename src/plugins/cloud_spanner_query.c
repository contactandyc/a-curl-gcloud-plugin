// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/cloud_spanner_query.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/curl_resource.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPANNER_QUERY_URL_FORMAT =
    "https://spanner.googleapis.com/v1/projects/%s/instances/%s/databases/%s/sessions/%s:executeSql";

typedef struct {
    curl_event_res_id token_id;
    curl_event_res_id session_id;
    curl_sink_interface_t *output;
    char *url;
    char *post_body;
} spanner_query_ctx_t;

static void spanner_output_destroy(void *userdata) {
    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) ctx->output->destroy(ctx->output);
    if (ctx->url) aml_free(ctx->url);
    if (ctx->post_body) aml_free(ctx->post_body);
    aml_free(ctx);
}

static bool spanner_on_prepare(curl_event_request_t *req) {
    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)req->plugin_data;

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    const char *session_name =
        (const char *)curl_event_res_peek(req->loop, ctx->session_id);

    if (!tok || !tok->access_token || !session_name || !*session_name) {
        fprintf(stderr, "[spanner_query] missing token or session name.\n");
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);
    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

static size_t spanner_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;
    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->write) return output->write(data, size, nmemb, output);
    }
    return size * nmemb;
}

static int spanner_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;
    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->complete) output->complete(output, req);
    }
    return 0;
}

static int spanner_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;
    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->failure) output->failure(result, http_code, output, req);
    }
    return 0;
}

bool curl_event_plugin_spanner_query_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    curl_event_res_id  token_id,
    curl_event_res_id  session_id,
    const char *sql_statement,
    curl_sink_interface_t *output_interface
) {
    if (!loop || !project_id || !instance_id || !database_id ||
        token_id == 0 || session_id == 0 || !sql_statement || !output_interface) {
        return false;
    }

    const char *session_name = (const char *)curl_event_res_peek(loop, session_id);
    if (!session_name || !*session_name) session_name = "SESSION_PLACEHOLDER";

    spanner_query_ctx_t *ctx = (spanner_query_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) return false;
    
    ctx->token_id = token_id;
    ctx->session_id = session_id;
    ctx->output = output_interface;
    ctx->url = aml_strdupf(SPANNER_QUERY_URL_FORMAT, project_id, instance_id, database_id, session_name);
    ctx->post_body = aml_strdupf("{\"sql\":\"%s\"}", sql_statement);

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->post_body);

    req->write_cb    = spanner_on_write;
    req->on_prepare  = spanner_on_prepare;
    req->on_complete = spanner_on_complete;
    req->on_failure  = spanner_on_failure;

    curl_event_request_plugin_data(req, ctx, spanner_output_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 5;

    curl_event_request_depend(req, token_id);
    curl_event_request_depend(req, session_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        spanner_output_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}
