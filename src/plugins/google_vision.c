// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/google_vision.h"
#include "a-curl-library/rate_manager.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GOOGLE_VISION_BASE_URL = "https://vision.googleapis.com/v1/images:annotate";

void curl_event_plugin_google_vision_set_rate(void) {
    rate_manager_set_limit("google_vision", 5, 10.0);
}

typedef struct {
    curl_event_res_id api_key_id;
    char *url;
    char *body;
    curl_sink_interface_t *output;
} gv_ctx_t;

static void gv_ctx_destroy(void *p) {
    gv_ctx_t *c = p;
    if(c->url) aml_free(c->url);
    if(c->body) aml_free(c->body);
    if(c->output && c->output->destroy) c->output->destroy(c->output);
    aml_free(c);
}

static char *build_web_detection_body(const char *image_url) {
    aml_pool_t *pool = aml_pool_init(16 * 1024);
    if (!pool) return NULL;

    ajson_t *root = ajsono(pool);
    ajson_t *requests = ajsona(pool);
    ajson_t *req = ajsono(pool);

    ajson_t *image = ajsono(pool);
    ajson_t *source = ajsono(pool);
    ajsono_append(source, "imageUri", ajson_encode_str(pool, image_url), false);
    ajsono_append(image, "source", source, false);
    ajsono_append(req, "image", image, false);

    ajson_t *features = ajsona(pool);
    ajson_t *feature = ajsono(pool);
    ajsono_append(feature, "type", ajson_encode_str(pool, "WEB_DETECTION"), false);
    ajsona_append(features, feature);
    ajsono_append(req, "features", features, false);

    ajsona_append(requests, req);
    ajsono_append(root, "requests", requests, false);

    const char *tmp = ajson_stringify(pool, root);
    char *out = tmp ? aml_strdup(tmp) : NULL;

    aml_pool_destroy(pool);
    return out;
}

static bool google_vision_on_prepare(curl_event_request_t *req) {
    gv_ctx_t *ctx = (gv_ctx_t *)req->plugin_data;

    const char *api_key = (const char *)curl_event_res_peek(req->loop, ctx->api_key_id);
    if (!api_key || !*api_key) return false;

    char *neu = aml_strdupf("%s?key=%s", ctx->url, api_key);
    if (!neu) return false;

    aml_free(ctx->url);
    ctx->url = neu;
    req->url = ctx->url;

    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

curl_event_request_t *curl_event_plugin_google_vision_init(
    curl_event_loop_t *loop,
    curl_event_res_id  api_key_id,
    const char *image_url,
    curl_sink_interface_t *output_interface
) {
    if (!loop || api_key_id == 0 || !image_url || !output_interface) return NULL;

    gv_ctx_t *ctx = aml_calloc(1, sizeof(*ctx));
    ctx->api_key_id = api_key_id;
    ctx->url = aml_strdup(GOOGLE_VISION_BASE_URL);
    ctx->body = build_web_detection_body(image_url);
    ctx->output = output_interface;

    if (!ctx->url || !ctx->body) {
        gv_ctx_destroy(ctx);
        return NULL;
    }

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, ctx->body);
    req->on_prepare = google_vision_on_prepare;
    req->rate_limit = "google_vision";

    curl_event_request_sink(req, output_interface, NULL);
    curl_event_request_plugin_data(req, ctx, gv_ctx_destroy);

    req->low_speed_limit = 1024;
    req->low_speed_time  = 15;
    req->max_retries     = 3;

    curl_event_request_depend(req, api_key_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        gv_ctx_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return NULL;
    }
    return req;
}
