// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/embeddings.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOOGLE_EMBEDDING_URL_FORMAT =
    "https://us-central1-aiplatform.googleapis.com/v1/projects/%s/locations/us-central1/publishers/google/models/%s:predict";

void curl_event_plugin_google_embed_set_rate(void) {
    rate_manager_set_limit("google_embed", 50, 24.5);
}

typedef struct google_embed_ctx_s {
    curl_event_res_id token_id;
    char *url;
    char *post_body;
} google_embed_ctx_t;

static void google_embed_ctx_destroy(void *userdata) {
    google_embed_ctx_t *ctx = (google_embed_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->url) aml_free(ctx->url);
    if (ctx->post_body) aml_free(ctx->post_body);
    aml_free(ctx);
}

static bool google_embed_on_prepare(curl_event_request_t *req) {
    google_embed_ctx_t *ctx = (google_embed_ctx_t *)req->plugin_data;

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!tok || !tok->access_token || !*tok->access_token) {
        return false;
    }

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);
    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

curl_event_request_t *curl_event_plugin_google_embed_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *model_id,
    int output_dimensionality,
    curl_event_res_id  token_id,
    char **input_text,
    size_t num_texts,
    curl_sink_interface_t *output_interface
) {
    if (!loop || !project_id || !model_id || token_id == 0 ||
        !input_text || num_texts == 0 || !output_interface) {
        return NULL;
    }

    char *url = aml_strdupf(GOOGLE_EMBEDDING_URL_FORMAT, project_id, model_id);
    if (!url) return NULL;

    aml_pool_t *pool = aml_pool_init(16 * 1024);
    if (!pool) {
        aml_free(url);
        return NULL;
    }

    ajson_t *root = ajsono(pool);

    if (output_dimensionality > 0) {
        ajson_t *parameters = ajsono(pool);
        ajsono_append(parameters, "outputDimensionality",
                      ajson_number(pool, output_dimensionality), false);
        ajsono_append(root, "parameters", parameters, false);
    }

    ajson_t *arr = ajsona(pool);
    for (size_t i = 0; i < num_texts; i++) {
        const char *s = input_text[i] ? input_text[i] : "";
        ajson_t *obj = ajsono(pool);
        ajsono_append(obj, "content", ajson_encode_str(pool, s), false);
        ajsona_append(arr, obj);
    }
    ajsono_append(root, "instances", arr, false);

    const char *json_tmp = ajson_stringify(pool, root);
    char *post_body = json_tmp ? aml_strdup(json_tmp) : NULL;
    aml_pool_destroy(pool);

    if (!post_body) {
        aml_free(url);
        return NULL;
    }

    google_embed_ctx_t *ctx = (google_embed_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) {
        aml_free(url);
        aml_free(post_body);
        return NULL;
    }

    ctx->token_id  = token_id;
    ctx->url       = url;
    ctx->post_body = post_body;

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->post_body);
    req->on_prepare = google_embed_on_prepare;
    req->rate_limit = "google_embed";

    curl_event_request_sink(req, output_interface, NULL);
    curl_event_request_plugin_data(req, ctx, google_embed_ctx_destroy);

    req->low_speed_limit = 1024;
    req->low_speed_time  = 15;
    req->max_retries     = 3;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        google_embed_ctx_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return NULL;
    }
    return req;
}
