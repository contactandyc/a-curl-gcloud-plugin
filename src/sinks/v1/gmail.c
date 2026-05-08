// SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#include "a-curl-gcloud-plugin/sinks/v1/gmail.h"
#include "a-json-library/ajson.h"
#include "a-memory-library/aml_buffer.h"
#include "a-memory-library/aml_pool.h"

// Lexbor
#include <lexbor/html/parser.h>
#include <lexbor/html/interface.h>
#include <lexbor/dom/interfaces/text.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---- Shared buffer sink scaffold ---- */
typedef struct {
    curl_sink_interface_t iface;
    aml_buffer_t *resp;
    void *cb;
    void *cb_arg;
} base_sink_t;

/* Safe initialization that handles rate_manager automatic retries */
static bool s_init(curl_sink_interface_t *iface, long cl) {
    base_sink_t *s = (void *)iface;
    if (s->resp) {
        aml_buffer_clear(s->resp); // Request was retried, reuse existing buffer
    } else {
        s->resp = aml_buffer_init(cl > 0 ? cl + 1 : 4096);
    }
    return s->resp != NULL;
}

static size_t s_write(const void *ptr, size_t sz, size_t nm, curl_sink_interface_t *iface) {
    base_sink_t *s = (void *)iface;
    aml_buffer_append(s->resp, ptr, sz * nm);
    return sz * nm;
}

static void s_destroy(curl_sink_interface_t *iface) {
    base_sink_t *s = (void *)iface;
    if (s->resp) aml_buffer_destroy(s->resp);
}

/* ---- String Cleaners & Parsers ---- */
static char *unescape_gmail_string(aml_pool_t *pool, const char *raw) {
    if (!raw) return NULL;
    size_t len = strlen(raw);
    char *out = aml_pool_alloc(pool, len + 1);
    char *p = out;

    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            if (raw[i+1] == '"') { *p++ = '"'; i++; }
            else if (raw[i+1] == 'u' && i + 5 < len && strncasecmp(&raw[i+2], "003c", 4) == 0) { *p++ = '<'; i += 5; }
            else if (raw[i+1] == 'u' && i + 5 < len && strncasecmp(&raw[i+2], "003e", 4) == 0) { *p++ = '>'; i += 5; }
            else if (raw[i+1] == 'u' && i + 5 < len && strncasecmp(&raw[i+2], "0026", 4) == 0) { *p++ = '&'; i += 5; }
            else { *p++ = raw[i]; }
        } else {
            *p++ = raw[i];
        }
    }
    *p = '\0';
    return out;
}

static gcloud_v1_gmail_contact_t *parse_contacts(aml_pool_t *pool, const char *clean_val, size_t *out_count) {
    if (!clean_val) { *out_count = 0; return NULL; }

    char *copy = aml_pool_strdup(pool, clean_val);
    size_t max_contacts = 1;
    for(char *p = copy; *p; p++) if (*p == ',') max_contacts++;

    gcloud_v1_gmail_contact_t *contacts = aml_pool_zalloc(pool, max_contacts * sizeof(*contacts));
    size_t count = 0;

    char *p = copy;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        char *start = p;
        bool in_quotes = false;
        char *email_start = NULL;
        char *email_end = NULL;

        while (*p) {
            if (*p == '"') in_quotes = !in_quotes;
            else if (!in_quotes && *p == '<') email_start = p + 1;
            else if (!in_quotes && *p == '>') email_end = p;
            else if (!in_quotes && *p == ',') break;
            p++;
        }

        bool has_comma = (*p == ',');
        *p = '\0';

        if (email_start && email_end && email_end > email_start) {
            *email_end = '\0';
            contacts[count].email = email_start;

            char *name_end = email_start - 2;
            while (name_end >= start && (*name_end == ' ' || *name_end == '\t' || *name_end == '"' || *name_end == '<')) {
                *name_end = '\0';
                name_end--;
            }
            while (*start == ' ' || *start == '\t' || *start == '"') start++;
            if (strlen(start) > 0) contacts[count].name = start;
        } else {
            while (*start == ' ' || *start == '\t' || *start == '"') start++;
            char *end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\t' || *end == '"')) {
                *end = '\0';
                end--;
            }
            if (strlen(start) > 0) contacts[count].email = start;
        }

        count++;
        if (has_comma) p++;
    }
    *out_count = count;
    return contacts;
}

/* ---- Lexbor HTML Parsing ---- */
static void walk_dom_for_text(lxb_dom_node_t *node, aml_buffer_t *out) {
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len = 0;
        const lxb_char_t *text = lxb_dom_node_text_content(node, &len);
        if (text && len > 0) aml_buffer_append(out, (const char *)text, len);
    }
    else if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_SCRIPT) return;
        if (node->local_name == LXB_TAG_P || node->local_name == LXB_TAG_DIV || node->local_name == LXB_TAG_BR) {
            aml_buffer_append(out, "\n", 1);
        }
    }

    lxb_dom_node_t *child = lxb_dom_node_first_child(node);
    while (child != NULL) {
        walk_dom_for_text(child, out);
        child = lxb_dom_node_next(child);
    }
}

static uint8_t *html_to_plain_text(aml_pool_t *pool, const uint8_t *html_data, size_t html_len, size_t *out_len) {
    lxb_html_document_t *document = lxb_html_document_create();
    if (!document) return NULL;

    lxb_status_t status = lxb_html_document_parse(document, (const lxb_char_t *)html_data, html_len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return NULL;
    }

    aml_buffer_t *out = aml_buffer_init(html_len / 2 + 1024);
    if (!out) {
        lxb_html_document_destroy(document);
        return NULL;
    }

    lxb_dom_node_t *body = lxb_dom_interface_node(lxb_html_document_body_element(document));
    if (body) walk_dom_for_text(body, out);

    aml_buffer_append(out, "", 1);
    *out_len = aml_buffer_length(out) - 1;
    uint8_t *result = (uint8_t *)aml_pool_strdup(pool, aml_buffer_data(out));

    aml_buffer_destroy(out);
    lxb_html_document_destroy(document);

    return result;
}

/* Robust, single-pass Web-Safe Base64 Decoder */
static uint8_t *b64dec_websafe(aml_pool_t *pool, const char *b64, size_t *out_len) {
    size_t len = strlen(b64);

    // Allocate max possible output size (3 bytes output for every 4 bytes input)
    uint8_t *buf = aml_pool_alloc(pool, (len / 4 + 1) * 3);
    size_t j = 0;

    static signed char tbl[256];
    static int inited = 0;
    if (!inited) {
        memset(tbl, -1, 256);
        const char *ff = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i=0; i<64; ++i) tbl[(unsigned char)ff[i]] = (signed char)i;
        inited = 1;
    }

    int val = 0;
    int valb = -8;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = b64[i];

        // Transparently convert web-safe characters to standard base64
        if (c == '-' || c == '+') c = '+';
        else if (c == '_' || c == '/') c = '/';
        else if (c == '=') break; // Reached padding, end of data

        int v = tbl[c];
        if (v == -1) continue; // CRITICAL FIX: Silently ignore \n, \r, and spaces!

        // Accumulate bits and write out bytes as they fill up
        val = (val << 6) | v;
        valb += 6;
        if (valb >= 0) {
            buf[j++] = (val >> valb) & 0xFF;
            valb -= 8;
        }
    }

    *out_len = j;
    return buf;
}

static const char *find_mime_part(ajson_t *part, const char *target_mime) {
    if (!part) return NULL;

    const char *mime = ajsono_scan_str(part, "mimeType", "");
    if (strcasecmp(mime, target_mime) == 0) {
        ajson_t *body = ajsono_scan(part, "body");
        return body ? ajsono_scan_str(body, "data", NULL) : NULL;
    }

    ajson_t *sub_parts = ajsono_scan(part, "parts");
    if (sub_parts && ajson_is_array(sub_parts)) {
        for (ajsona_t *el = ajsona_first(sub_parts); el; el = ajsona_next(el)) {
            const char *found = find_mime_part(el->value, target_mime);
            if (found) return found;
        }
    }
    return NULL;
}

static size_t count_attachments(ajson_t *part) {
    if (!part) return 0;
    size_t count = 0;

    ajson_t *body = ajsono_scan(part, "body");
    const char *att_id = body ? ajsono_scan_str(body, "attachmentId", NULL) : NULL;
    const char *filename = ajsono_scan_str(part, "filename", "");

    if (att_id && filename[0] != '\0') count++;

    ajson_t *parts = ajsono_scan(part, "parts");
    if (parts && ajson_is_array(parts)) {
        for (ajsona_t *n = ajsona_first(parts); n; n = ajsona_next(n)) {
            count += count_attachments(n->value);
        }
    }
    return count;
}

static void extract_attachments(aml_pool_t *pool, ajson_t *part, gcloud_v1_gmail_attachment_ref_t *atts, size_t *idx) {
    if (!part) return;

    ajson_t *body = ajsono_scan(part, "body");
    const char *att_id = body ? ajsono_scan_str(body, "attachmentId", NULL) : NULL;
    const char *filename = ajsono_scan_str(part, "filename", "");

    if (att_id && filename[0] != '\0') {
        atts[*idx].attachment_id = aml_pool_strdup(pool, att_id);
        atts[*idx].filename = aml_pool_strdup(pool, filename);
        atts[*idx].mime_type = aml_pool_strdup(pool, ajsono_scan_str(part, "mimeType", "application/octet-stream"));
        atts[*idx].size = (size_t)ajsono_scan_int(body, "size", 0);
        (*idx)++;
    }

    ajson_t *parts = ajsono_scan(part, "parts");
    if (parts && ajson_is_array(parts)) {
        for (ajsona_t *n = ajsona_first(parts); n; n = ajsona_next(n)) {
            extract_attachments(pool, n->value, atts, idx);
        }
    }
}

/* ---- Messages.List Sink ---- */
typedef struct {
    base_sink_t b;
    gcloud_v1_gmail_messages_list_cb cb;
} sink_list_t;

static void list_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_list_t *s = (void *)iface;
    if (http == 429 || http == 403) return;  // rate_manager handles backoff; silently ignore here.
    fprintf(stderr, "[gcloud.gmail.list] HTTP %ld, CURL %d\n", http, res);
    if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0, NULL, 0);
}

static void list_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_list_t *s = (void *)iface;
    aml_pool_t *pool = iface->pool;

    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
    if (!json || ajson_is_error(json)) {
        if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0, NULL, 0);
        return;
    }

    ajson_t *arr = ajsono_scan(json, "messages");
    size_t n = arr && ajson_is_array(arr) ? ajsona_count(arr) : 0;
    gcloud_v1_gmail_message_ref_t *msgs = n ? aml_pool_calloc(pool, n, sizeof(*msgs)) : NULL;

    size_t i = 0;
    if (arr) {
        for (ajsona_t *el = ajsona_first(arr); el; el = ajsona_next(el)) {
            msgs[i].id = aml_pool_strdup(pool, ajson_to_strd(pool, ajsono_scan(el->value, "id"), ""));
            msgs[i].thread_id = aml_pool_strdup(pool, ajson_to_strd(pool, ajsono_scan(el->value, "threadId"), ""));
            i++;
        }
    }

    const char *next_tok = ajsono_scan_str(json, "nextPageToken", NULL);
    uint64_t estimate = (uint64_t)ajsono_scan_int(json, "resultSizeEstimate", 0);

    if (s->cb) s->cb(s->b.cb_arg, req, true, msgs, n, next_tok, estimate);
}

curl_sink_interface_t *gcloud_v1_gmail_messages_list_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_messages_list_cb cb, void *cb_arg)
{
    if (!req) return NULL;
    sink_list_t *s = aml_pool_zalloc(req->pool, sizeof *s);
    s->cb = cb; s->b.cb_arg = cb_arg;
    s->b.iface.pool    = req->pool;
    s->b.iface.init    = s_init;
    s->b.iface.write   = s_write;
    s->b.iface.failure = list_failure;
    s->b.iface.complete = list_complete;
    s->b.iface.destroy = s_destroy;
    curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
    return (curl_sink_interface_t *)s;
}

/* ---- Messages.Get Sink ---- */
typedef struct {
    base_sink_t b;
    gcloud_v1_gmail_message_get_cb cb;
    bool extract_html_text;
} sink_get_t;

static void get_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_get_t *s = (void *)iface;
    if (http == 429 || http == 403) return;  // rate_manager handles backoff; silently ignore here.

    fprintf(stderr, "[gcloud.gmail.get] HTTP %ld, CURL %d\n", http, res);
    if (s->cb) s->cb(s->b.cb_arg, req, false, NULL);
}

static void get_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_get_t *s = (void *)iface;
    aml_pool_t *pool = iface->pool;

    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
    if (!json || ajson_is_error(json)) {
        if (s->cb) s->cb(s->b.cb_arg, req, false, NULL);
        return;
    }

    gcloud_v1_gmail_message_t *msg = aml_pool_zalloc(pool, sizeof(*msg));
    msg->id = ajsono_scan_str(json, "id", NULL);
    msg->thread_id = ajsono_scan_str(json, "threadId", NULL);

    const char *raw_snippet = ajsono_scan_str(json, "snippet", NULL);
    msg->snippet = unescape_gmail_string(pool, raw_snippet);

    const char *date_str = ajsono_scan_str(json, "internalDate", "0");
    msg->internal_date = strtoull(date_str, NULL, 10);

    ajson_t *labels = ajsono_scan(json, "labelIds");
    if (labels && ajson_is_array(labels)) {
        msg->num_labels = ajsona_count(labels);
        if (msg->num_labels > 0) {
            msg->label_ids = aml_pool_alloc(pool, msg->num_labels * sizeof(char*));
            size_t idx = 0;
            for (ajsona_t *l = ajsona_first(labels); l; l = ajsona_next(l)) {
                msg->label_ids[idx++] = aml_pool_strdup(pool, ajson_to_strd(pool, l->value, ""));
            }
        }
    }

    ajson_t *payload = ajsono_scan(json, "payload");
    if (payload) {
        ajson_t *headers = ajsono_scan(payload, "headers");
        if (headers && ajson_is_array(headers)) {
            msg->num_headers = ajsona_count(headers);
            msg->header_names = aml_pool_alloc(pool, msg->num_headers * sizeof(char*));
            msg->header_values = aml_pool_alloc(pool, msg->num_headers * sizeof(char*));

            size_t i = 0;
            for (ajsona_t *el = ajsona_first(headers); el; el = ajsona_next(el)) {
                msg->header_names[i] = ajsono_scan_str(el->value, "name", "");

                const char *raw_val = ajsono_scan_str(el->value, "value", "");
                char *clean_val = unescape_gmail_string(pool, raw_val);
                msg->header_values[i] = clean_val;

                if (strcasecmp(msg->header_names[i], "Subject") == 0) {
                    msg->subject = clean_val;
                } else if (strcasecmp(msg->header_names[i], "From") == 0) {
                    size_t count = 0;
                    msg->from = parse_contacts(pool, clean_val, &count);
                } else if (strcasecmp(msg->header_names[i], "To") == 0) {
                    msg->to = parse_contacts(pool, clean_val, &msg->num_to);
                } else if (strcasecmp(msg->header_names[i], "Cc") == 0) {
                    msg->cc = parse_contacts(pool, clean_val, &msg->num_cc);
                } else if (strcasecmp(msg->header_names[i], "Bcc") == 0) {
                    msg->bcc = parse_contacts(pool, clean_val, &msg->num_bcc);
                } else if (strcasecmp(msg->header_names[i], "Delivered-To") == 0) {
                    msg->delivered_to = clean_val;
                } else if (strcasecmp(msg->header_names[i], "List-Id") == 0 ||
                           strcasecmp(msg->header_names[i], "X-Google-Group-Id") == 0 ||
                           strcasecmp(msg->header_names[i], "Mailing-List") == 0) {
                    msg->list_id = clean_val;
                    msg->is_group = true;
                }
                i++;
            }
        }

        bool is_html = false;
        const char *b64 = find_mime_part(payload, "text/html");
        if (b64) {
            is_html = true;
        } else {
            b64 = find_mime_part(payload, "text/plain");
        }

        if (!b64) {
            ajson_t *body = ajsono_scan(payload, "body");
            b64 = body ? ajsono_scan_str(body, "data", NULL) : NULL;
        }

        if (b64) {
            size_t raw_len = 0;
            uint8_t *raw_data = b64dec_websafe(pool, b64, &raw_len);

            if (is_html && s->extract_html_text) {
                size_t text_len = 0;
                uint8_t *text_data = html_to_plain_text(pool, raw_data, raw_len, &text_len);
                if (text_data) {
                    msg->body_data = text_data;
                    msg->body_len = text_len;
                } else {
                    msg->body_data = raw_data;
                    msg->body_len = raw_len;
                }
            } else {
                msg->body_data = raw_data;
                msg->body_len = raw_len;
            }
        }

        msg->num_attachments = count_attachments(payload);
        if (msg->num_attachments > 0) {
            msg->attachments = aml_pool_zalloc(pool, msg->num_attachments * sizeof(gcloud_v1_gmail_attachment_ref_t));
            size_t idx = 0;
            extract_attachments(pool, payload, msg->attachments, &idx);
        }

    } else {
        const char *raw_b64 = ajsono_scan_str(json, "raw", NULL);
        if (raw_b64) {
            msg->body_data = b64dec_websafe(pool, raw_b64, &msg->body_len);
        }
    }

    if (s->cb) s->cb(s->b.cb_arg, req, true, msg);
}

curl_sink_interface_t *gcloud_v1_gmail_message_get_sink(
    curl_event_request_t *req,
    bool extract_html_text,
    gcloud_v1_gmail_message_get_cb cb, void *cb_arg)
{
    if (!req) return NULL;
    sink_get_t *s = aml_pool_zalloc(req->pool, sizeof *s);
    s->cb = cb;
    s->b.cb_arg = cb_arg;
    s->extract_html_text = extract_html_text;
    s->b.iface.pool    = req->pool;
    s->b.iface.init    = s_init;
    s->b.iface.write   = s_write;
    s->b.iface.failure = get_failure;
    s->b.iface.complete = get_complete;
    s->b.iface.destroy = s_destroy;
    curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
    return (curl_sink_interface_t *)s;
}

/* ---- Attachments.Get Sink ---- */
typedef struct {
    base_sink_t b;
    gcloud_v1_gmail_attachment_get_cb cb;
} sink_att_t;

static void att_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_att_t *s = (void *)iface;
    if (http == 429 || http == 403) return; // Silent retry

    fprintf(stderr, "[gcloud.gmail.att] HTTP %ld, CURL %d\n", http, res);
    if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0);
}

static void att_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_att_t *s = (void *)iface;
    aml_pool_t *pool = iface->pool;

    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
    if (!json || ajson_is_error(json)) {
        if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0);
        return;
    }

    const char *b64_data = ajsono_scan_str(json, "data", NULL);
    if (!b64_data) {
        if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, 0);
        return;
    }

    size_t file_len = 0;
    uint8_t *file_data = b64dec_websafe(pool, b64_data, &file_len);

    if (s->cb) s->cb(s->b.cb_arg, req, true, file_data, file_len);
}

curl_sink_interface_t *gcloud_v1_gmail_attachment_get_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_attachment_get_cb cb, void *cb_arg)
{
    if (!req) return NULL;
    sink_att_t *s = aml_pool_zalloc(req->pool, sizeof *s);
    s->cb = cb;
    s->b.cb_arg = cb_arg;
    s->b.iface.pool    = req->pool;
    s->b.iface.init    = s_init;
    s->b.iface.write   = s_write;
    s->b.iface.failure = att_failure;
    s->b.iface.complete = att_complete;
    s->b.iface.destroy = s_destroy;
    curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
    return (curl_sink_interface_t *)s;
}

/* ---- Message ID Sink (Send, Trash, Delete) ---- */
typedef struct {
    base_sink_t b;
    gcloud_v1_gmail_message_id_cb cb;
} sink_msg_id_t;

static void msg_id_failure(CURLcode res, long http, curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_msg_id_t *s = (void *)iface;
    if (http == 429 || http == 403) return; // Silent retry

    fprintf(stderr, "[gcloud.gmail.id_sink] HTTP %ld, CURL %d\n", http, res);
    if (s->cb) s->cb(s->b.cb_arg, req, false, NULL, NULL);
}

static void msg_id_complete(curl_sink_interface_t *iface, curl_event_request_t *req) {
    sink_msg_id_t *s = (void *)iface;
    aml_pool_t *pool = iface->pool;

    // Safely NUL-terminate the response buffer
    aml_buffer_append(s->b.resp, "", 1);

    ajson_t *json = ajson_parse_string(pool, aml_buffer_data(s->b.resp));
    if (!json || ajson_is_error(json)) {
        // Because the event loop ONLY calls complete() on 2xx responses,
        // an empty or non-JSON body (like from a DELETE/TRASH) is a guaranteed success!
        if (s->cb) s->cb(s->b.cb_arg, req, true, NULL, NULL);
        return;
    }

    const char *id = ajsono_scan_str(json, "id", NULL);
    const char *thread_id = ajsono_scan_str(json, "threadId", NULL);

    if (s->cb) s->cb(s->b.cb_arg, req, true, id, thread_id);
}

curl_sink_interface_t *gcloud_v1_gmail_message_id_sink(
    curl_event_request_t *req,
    gcloud_v1_gmail_message_id_cb cb, void *cb_arg)
{
    if (!req) return NULL;
    sink_msg_id_t *s = aml_pool_zalloc(req->pool, sizeof *s);
    s->cb = cb;
    s->b.cb_arg = cb_arg;
    s->b.iface.pool    = req->pool;
    s->b.iface.init    = s_init;
    s->b.iface.write   = s_write;
    s->b.iface.failure = msg_id_failure;
    s->b.iface.complete = msg_id_complete;
    s->b.iface.destroy = s_destroy;
    curl_event_request_sink(req, (curl_sink_interface_t *)s, NULL);
    return (curl_sink_interface_t *)s;
}