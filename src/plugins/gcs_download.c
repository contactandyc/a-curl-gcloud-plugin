// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/gcs_download.h"
#include "a-curl-gcloud-plugin/plugins/token.h"   // gcloud_token_payload_t
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *GCS_STORAGE_URL_FORMAT = "https://storage.googleapis.com/%s/%s";

static void swap_https_for_http(char *url) {
    if (!url) return;
    if (strncmp(url, "https", 5) == 0) {
        for (char *p = url + 4; *p; ++p) {
            *p = *(p + 1);
        }
    }
}

typedef struct gcs_ctx_s {
    curl_event_res_id        token_id;
    char                    *url;
    curl_sink_interface_t *output;
} gcs_ctx_t;

static void gcs_ctx_destroy(void *userdata) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)userdata;
    if (!ctx) return;
    if (ctx->output && ctx->output->destroy) {
        ctx->output->destroy(ctx->output);
    }
    if (ctx->url) aml_free(ctx->url);
    aml_free(ctx);
}

static bool gcs_on_prepare(curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->plugin_data;

    const gcloud_token_payload_t *tok =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, ctx->token_id);
    if (!tok || !tok->access_token) {
        fprintf(stderr, "[gcs_download] Missing token payload (dep not ready/failed).\n");
        return false;
    }

    if (tok->metadata_flavor) {
        swap_https_for_http(ctx->url);
    }
    req->url = ctx->url;

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", tok->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);

    return true;
}

static size_t gcs_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    gcs_ctx_t *ctx = (gcs_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;

    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->write) {
            return output->write(data, size, nmemb, output);
        }
    }
    return size * nmemb;
}

static int gcs_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;

    gcs_ctx_t *ctx = (gcs_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;

    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->complete) {
            output->complete(output, req);
        }
    }
    return 0; /* success */
}

static int gcs_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;

    gcs_ctx_t *ctx = (gcs_ctx_t *)req->plugin_data;
    curl_sink_interface_t *output = ctx ? ctx->output : NULL;

    if (output) {
        if (!req->sink_initialized && output->init) {
            output->init(output, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (output->failure) {
            output->failure(result, http_code, output, req);
        }
    }

    fprintf(stderr, "[gcs_download] Download failed %s (CURLcode:%d, HTTP:%ld)\n",
            req->url ? req->url : "(null)", (int)result, http_code);

    if (http_code == 401) {
        return -1;
    }
    return 0;
}

bool curl_event_plugin_gcs_download_init(
    curl_event_loop_t *loop,
    const char *bucket,
    const char *object,
    curl_event_res_id  token_id,
    curl_sink_interface_t *output_interface,
    long max_download_size
) {
    if (!loop || !bucket || !object || token_id == 0 || !output_interface) {
        fprintf(stderr, "[gcs_download_init] Invalid arguments.\n");
        return false;
    }

    char url_buf[1024];
    snprintf(url_buf, sizeof(url_buf), GCS_STORAGE_URL_FORMAT, bucket, object);

    gcs_ctx_t *ctx = (gcs_ctx_t *)aml_calloc(1, sizeof(*ctx));
    if (!ctx) return false;

    ctx->token_id = token_id;
    ctx->url      = aml_strdup(url_buf);
    ctx->output   = output_interface;

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, ctx->url);
    curl_event_request_method(req, "GET");

    req->write_cb        = gcs_on_write;
    req->on_prepare      = gcs_on_prepare;
    req->on_complete     = gcs_on_complete;
    req->on_failure      = gcs_on_failure;

    curl_event_request_plugin_data(req, ctx, gcs_ctx_destroy);

    req->low_speed_limit   = 1024;   /* 1 KB/s */
    req->low_speed_time    = 60;     /* 60 seconds */
    req->max_retries       = 5;
    req->max_download_size = max_download_size;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        fprintf(stderr, "[gcs_download_init] Failed to enqueue request.\n");
        gcs_ctx_destroy(ctx);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }

    return true;
}
