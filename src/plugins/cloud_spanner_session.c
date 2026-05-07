// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/plugins/cloud_spanner_session.h"
#include "a-curl-gcloud-plugin/plugins/token.h"
#include "a-curl-library/curl_resource.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SPANNER_SESSION_URL_FORMAT =
    "https://spanner.googleapis.com/v1/projects/%s/instances/%s/databases/%s/sessions";

typedef struct curl_event_plugin_spanner_session_s {
    curl_event_loop_t *loop;
    char *project_id;
    char *instance_id;
    char *database_id;
    curl_event_res_id token_id;
    curl_event_res_id session_id;
    char  *response_buffer;
    size_t response_len;
    size_t response_capacity;
} curl_event_plugin_spanner_session_t;

static bool spanner_session_on_prepare(curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->plugin_data;

    const gcloud_token_payload_t *p =
        (const gcloud_token_payload_t *)curl_event_res_peek(req->loop, plugin->token_id);
    if (!p || !p->access_token) return false;

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", p->access_token);
    curl_event_request_set_header(req, "Authorization", auth_header);
    curl_event_request_set_header(req, "Content-Type", "application/json");
    return true;
}

static size_t spanner_session_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->plugin_data;

    size_t total = size * nmemb;
    if (plugin->response_len + total + 1 > plugin->response_capacity) {
        size_t new_capacity = (plugin->response_len + total + 1) * 2;
        char *new_buffer = (char *)realloc(plugin->response_buffer, new_capacity);
        if (!new_buffer) return 0;
        plugin->response_buffer = new_buffer;
        plugin->response_capacity = new_capacity;
    }
    memcpy(plugin->response_buffer + plugin->response_len, data, total);
    plugin->response_len += total;
    plugin->response_buffer[plugin->response_len] = '\0';
    return total;
}

static int spanner_session_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle;
    curl_event_plugin_spanner_session_t *plugin =
        (curl_event_plugin_spanner_session_t *)req->plugin_data;

    aml_pool_t *pool = aml_pool_init(1024);
    ajson_t *json_response = ajson_parse_string(pool, plugin->response_buffer);
    plugin->response_len = 0; 

    if (ajson_is_error(json_response)) {
        aml_pool_destroy(pool);
        return 2; 
    }

    char *session_name = ajson_extract_string(pool, ajsono_scan(json_response, "name"));
    if (!session_name) {
        aml_pool_destroy(pool);
        return 2; 
    }

    curl_event_res_publish_str(req->loop, plugin->session_id, session_name);
    aml_pool_destroy(pool);
    return 0;
}

static int spanner_session_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle;
    return 0;
}

static void spanner_session_destroy(void *userdata) {
    curl_event_plugin_spanner_session_t *plugin = (curl_event_plugin_spanner_session_t *)userdata;
    if (!plugin) return;
    aml_free(plugin->project_id);
    aml_free(plugin->instance_id);
    aml_free(plugin->database_id);
    aml_free(plugin->response_buffer);
    aml_free(plugin);
}

bool curl_event_plugin_spanner_session_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *instance_id,
    const char *database_id,
    curl_event_res_id  token_id,
    curl_event_res_id  session_id
) {
    if (!loop || !project_id || !instance_id || !database_id || token_id == 0 || session_id == 0) return false;

    curl_event_plugin_spanner_session_t *plugin = aml_calloc(1, sizeof(*plugin));
    if (!plugin) return false;

    plugin->loop        = loop;
    plugin->project_id  = aml_strdup(project_id);
    plugin->instance_id = aml_strdup(instance_id);
    plugin->database_id = aml_strdup(database_id);
    plugin->token_id    = token_id;
    plugin->session_id  = session_id;
    plugin->response_capacity = 1024;
    plugin->response_buffer   = (char *)aml_calloc(1, plugin->response_capacity);

    char *url  = aml_strdupf(SPANNER_SESSION_URL_FORMAT, project_id, instance_id, database_id);
    char *body = aml_strdup("{}");

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, url);
    curl_event_request_method(req, "POST");
    curl_event_request_body(req, body);

    req->write_cb    = spanner_session_on_write;
    req->on_prepare  = spanner_session_on_prepare;
    req->on_complete = spanner_session_on_complete;
    req->on_failure  = spanner_session_on_failure;

    curl_event_request_plugin_data(req, plugin, spanner_session_destroy);

    req->connect_timeout  = 10;
    req->transfer_timeout = 60;
    req->max_retries      = 3;

    curl_event_request_depend(req, token_id);

    if (!curl_event_request_submit(loop, req, 0)) {
        aml_free(url);
        aml_free(body);
        spanner_session_destroy(plugin);
        curl_event_request_destroy_unsubmitted(req);
        return false;
    }
    return true;
}
