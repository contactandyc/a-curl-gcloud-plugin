// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/google_custom_search.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOOGLE_CUSTOM_SEARCH_URL_FORMAT =
    "https://www.googleapis.com/customsearch/v1?cx=%s&q=%s";

void curl_event_plugin_google_custom_search_set_rate(void) {
    rate_manager_set_limit("google_custom_search", 5, 9.0);
}

typedef struct gcs_ctx_s {
    curl_event_res_id api_key_id;
    char *url;
} gcs_ctx_t;

static void gcs_ctx_destroy(void *userdata) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->url) aml_free(ctx->url);
    aml_free(ctx);
}

static bool google_custom_search_on_prepare(curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->plugin_data;

    const char *api_key = (const char *)curl_event_res_peek(req->loop, ctx->api_key_id);
    if (!api_key || !*api_key) {
        fprintf(stderr, "[google_custom_search] Missing API key.\n");
        return false;
    }

    char *new_url = aml_strdupf("%s&key=%s", ctx->url, api_key);
    if (!new_url) return false;

    aml_free(ctx->url);
    ctx->url = new_url;
    req->url = ctx->url;
    return true;
}

curl_event_request_t *curl_event_plugin_google_custom_search_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *search_engine_id,
    const char *query,
    curl_sink_interface_t *output_interface
) {
    if (!loop || api_key_id == 0 || !search_engine_id || !query || !output_interface) {
        return NULL;
    }

    char *url = aml_strdupf(GOOGLE_CUSTOM_SEARCH_URL_FORMAT, search_engine_id, query);
    if (!url) return NULL;

    gcs_ctx_t *ctx = (gcs_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        aml_free(url);
        return NULL;
    }

    ctx->api_key_id = api_key_id;
    ctx->url        = url;

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "GET");
    req->on_prepare   = google_custom_search_on_prepare;
    req->rate_limit   = "google_custom_search";

    curl_event_request_sink(req, output_interface, NULL);
    curl_event_request_plugin_data(req, ctx, gcs_ctx_destroy);

    req->low_speed_limit = 1024;
    req->low_speed_time  = 15;
    req->max_retries     = 3;

    curl_event_request_depend(req, api_key_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        gcs_ctx_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return NULL;
    }
    return req;
}
