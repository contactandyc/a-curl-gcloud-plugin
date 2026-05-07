// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include "the-io-library/io.h"
#include "a-memory-library/aml_buffer.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include "the-macro-library/macro_time.h"

#include <jwt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pwd.h>
#include <unistd.h>
#include <limits.h>

typedef struct curl_event_plugin_gcloud_token_s {
    curl_event_loop_t *loop;
    char *private_key;
    char *client_email;
    char *client_id;
    char *client_secret;
    char *refresh_token;
    curl_event_res_id token_id;
    bool metadata_flavor;
    int  token_refreshes;
    time_t expires_at;
    aml_buffer_t *response_bh;
} curl_event_plugin_gcloud_token_t;

void gcloud_token_payload_free(void *vp) {
    gcloud_token_payload_t *p = (gcloud_token_payload_t *)vp;
    if (!p) return;
    if (p->access_token) aml_free(p->access_token);
    aml_free(p);
}

static const char *GOOGLE_OAUTH_TOKEN_URL = "https://oauth2.googleapis.com/token";
static const char *GOOGLE_METADATA_TOKEN_URL = "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/token";

static char *find_key_file(const char *filename) {
    char cwd[PATH_MAX];
    char path[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd))) {
        while (1) {
            snprintf(path, sizeof(path)-2, "%s/%s", cwd, filename);
            if (access(path, F_OK) == 0) return aml_strdup(path);
            char *last_slash = strrchr(cwd, '/');
            if (!last_slash) break;
            *last_slash = '\0';
        }
    }

    const char *home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home_dir = pw->pw_dir;
    }
    if (home_dir) {
        snprintf(path, sizeof(path)-100, "%s/.config/gcloud/application_default_credentials.json", home_dir);
        if (access(path, F_OK) == 0) return aml_strdup(path);
    }
    return NULL;
}

static bool parse_service_account_key(const char *key_file, curl_event_plugin_gcloud_token_t *h) {
    aml_pool_t *pool = aml_pool_init(1024);
    size_t length = 0;
    char *key_data = io_pool_read_file(pool, &length, key_file);
    if (!key_data) { aml_pool_destroy(pool); return false; }

    ajson_t *json = ajson_parse_string(pool, key_data);
    if (ajson_is_error(json)) { aml_pool_destroy(pool); return false; }

    char *type = ajsono_scan_strd(pool, json, "type", NULL);
    if(!type) { aml_pool_destroy(pool); return false; }

    if(!strcmp(type, "service_account")) {
        h->client_email = aml_strdup(ajsono_scan_strd(pool, json, "client_email", NULL));
        h->private_key = aml_strdup(ajsono_scan_strd(pool, json, "private_key", NULL));
    } else if(!strcmp(type, "authorized_user")) {
        h->client_id = aml_strdup(ajsono_scan_strd(pool, json, "client_id", NULL));
        h->client_secret = aml_strdup(ajsono_scan_strd(pool, json, "client_secret", NULL));
        h->refresh_token = aml_strdup(ajsono_scan_strd(pool, json, "refresh_token", NULL));
    } else {
        aml_pool_destroy(pool);
        return false;
    }
    aml_pool_destroy(pool);
    return true;
}

static jwk_item_t *make_rsa_jwk(const char *pem, jwt_alg_t alg) {
    char jwk_json[4096];
    snprintf(jwk_json, sizeof(jwk_json), "{ \"kty\":\"RSA\", \"use\":\"sig\", \"alg\":\"%s\", \"pem\":\"%s\" }", jwt_alg_str(alg), pem);
    jwk_set_t *set = jwks_load(NULL, jwk_json);
    if (!set || jwks_error(set)) return NULL;
    return (jwk_item_t *)jwks_item_get(set, 0);
}

static char *generate_jwt(const curl_event_plugin_gcloud_token_t *h) {
    jwt_builder_t *b = jwt_builder_new();
    if (!b) return NULL;

    jwk_item_t *key = make_rsa_jwk(h->private_key, JWT_ALG_RS256);
    if (!key || jwt_builder_setkey(b, JWT_ALG_RS256, key) != 0) {
        jwt_builder_free(b);
        return NULL;
    }

    time_t now = time(NULL);
    jwt_value_t v;
    jwt_set_SET_STR(&v, "iss",   h->client_email);                                 jwt_builder_claim_set(b, &v);
    jwt_set_SET_STR(&v, "scope", "https://www.googleapis.com/auth/cloud-platform"); jwt_builder_claim_set(b, &v);
    jwt_set_SET_STR(&v, "aud",   "https://oauth2.googleapis.com/token");           jwt_builder_claim_set(b, &v);
    jwt_set_SET_INT(&v, "iat",   now);                                             jwt_builder_claim_set(b, &v);
    jwt_set_SET_INT(&v, "exp",   now + 3600);                                      jwt_builder_claim_set(b, &v);

    char *token = jwt_builder_generate(b);
    jwt_builder_free(b);
    return token;
}

static size_t token_write_cb(void *contents, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_event_plugin_gcloud_token_t *ctx = (curl_event_plugin_gcloud_token_t *)req->plugin_data;
    size_t total = size * nmemb;
    if (total == 0) return 0;
    aml_buffer_append(ctx->response_bh, contents, total);
    return total;
}

static bool gcloud_on_prepare(curl_event_request_t *req) {
    curl_event_plugin_gcloud_token_t *gct = (curl_event_plugin_gcloud_token_t *)req->plugin_data;

    if (gct->private_key && gct->client_email) {
        char *jwt = generate_jwt(gct);
        if (!jwt) return false;
        static const char *fmt = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=%s";
        size_t needed = strlen(fmt) + strlen(jwt) + 1;
        char *post = (char *)aml_zalloc(needed);
        sprintf(post, fmt, jwt);
        aml_free(req->post_data);
        req->post_data = post;
        free(jwt);
    } else if (gct->client_id && gct->client_secret && gct->refresh_token) {
        const char *fmt = "grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s";
        size_t needed = strlen(fmt) + strlen(gct->client_id) + strlen(gct->client_secret) + strlen(gct->refresh_token) + 1;
        char *post = (char *)aml_zalloc(needed);
        snprintf(post, needed, fmt, gct->client_id, gct->client_secret, gct->refresh_token);
        aml_free(req->post_data);
        req->post_data = post;
    }
    return true;
}

static int gcloud_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle; (void)result; (void)http_code; (void)req;
    return 2; 
}

static int gcloud_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    curl_event_plugin_gcloud_token_t *gct = (curl_event_plugin_gcloud_token_t *)req->plugin_data;

    gct->expires_at   = time(NULL);
    req->next_retry_at = 0;

    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(gct->response_bh));
    aml_buffer_reset(gct->response_bh, 0);

    if (ajson_is_error(json)) {
        aml_pool_destroy(pool);
        return 2;
    }

    char *access_token = ajson_extract_string(pool, ajsono_scan(json, "access_token"));
    int   expires_in   = ajsono_scan_int(json, "expires_in", 0);
    
    if (!access_token || !expires_in) {
        aml_pool_destroy(pool);
        return 2;
    }

    curl_event_res_publish_str(req->loop, gct->token_id, access_token);

    gcloud_token_payload_t *payload = (gcloud_token_payload_t *)aml_calloc(1, sizeof(*payload));
    payload->access_token   = aml_strdup(access_token);
    payload->metadata_flavor= gct->metadata_flavor;
    payload->expires_at     = time(NULL) + expires_in;

    curl_event_res_publish(req->loop, gct->token_id, payload, gcloud_token_payload_free);

    const int lead_seconds = 360;
    int next_refresh = (expires_in > lead_seconds + 1) ? (expires_in - lead_seconds) : (expires_in / 2);

    gct->token_refreshes++;
    gct->expires_at = macro_now_add_seconds(next_refresh);

    aml_pool_destroy(pool);
    return next_refresh;
}

static void gcloud_token_destroy(void *userdata) {
    curl_event_plugin_gcloud_token_t *gct = (curl_event_plugin_gcloud_token_t *)userdata;
    if (!gct) return;
    if (gct->private_key)   aml_free(gct->private_key);
    if (gct->client_email)  aml_free(gct->client_email);
    if (gct->client_id)     aml_free(gct->client_id);
    if (gct->client_secret) aml_free(gct->client_secret);
    if (gct->refresh_token) aml_free(gct->refresh_token);
    if (gct->response_bh)   aml_buffer_destroy(gct->response_bh);
    aml_free(gct);
}

curl_event_request_t *curl_event_plugin_gcloud_token_init(curl_event_loop_t *loop,
                                                          const char *key_filename,
                                                          curl_event_res_id token_id,
                                                          bool should_refresh) {
    if (!loop || !token_id) return NULL;

    char *key_file = key_filename ? find_key_file(key_filename) : NULL;

    curl_event_plugin_gcloud_token_t *gct = (curl_event_plugin_gcloud_token_t *)aml_zalloc(sizeof(*gct));
    if (!gct) return NULL;
    
    gct->loop            = loop;
    gct->token_id        = token_id;
    gct->token_refreshes = 0;
    gct->response_bh     = aml_buffer_init(1024);
    gct->metadata_flavor = false;

    if (key_file) {
        parse_service_account_key(key_file, gct);
        aml_free(key_file);
    }

    curl_event_request_t *req = curl_event_request_init(0);
    req->loop = loop;

    struct curl_slist *headers = NULL;
    if (!gct->client_email && !gct->client_id) {
        headers = curl_slist_append(headers, "Metadata-Flavor: Google");
        curl_event_request_url(req, GOOGLE_METADATA_TOKEN_URL);
        curl_event_request_method(req, "GET");
        gct->metadata_flavor = true;
    } else {
        curl_event_request_url(req, GOOGLE_OAUTH_TOKEN_URL);
        curl_event_request_method(req, "POST");
        req->on_prepare = gcloud_on_prepare;
    }

    req->headers         = headers;
    req->on_complete     = gcloud_on_complete;
    req->on_failure      = gcloud_on_failure;
    req->should_refresh  = should_refresh;
    req->max_retries     = 10;
    req->connect_timeout = 10;
    req->transfer_timeout= 30;

    curl_event_request_plugin_data(req, gct, gcloud_token_destroy);
    req->write_cb        = token_write_cb;

    if (!curl_event_request_submit(loop, req, 0)) {
        gcloud_token_destroy(gct);
        curl_event_request_destroy_unsubmitted(req);
        return NULL;
    }
    return req;
}
