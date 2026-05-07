// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef A_CURL_GCLOUD_PLUGIN_PUBSUB_PULL_H
#define A_CURL_GCLOUD_PLUGIN_PUBSUB_PULL_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"   /* curl_event_res_id */
#include "a-curl-library/curl_event_request.h"     /* curl_sink_interface_t */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Pub/Sub pull plugin.
 *
 * @param loop           The curl_event_loop instance.
 * @param project_id     The Google Cloud project ID.
 * @param subscription_id The Pub/Sub subscription ID (or full path).
 * @param token_id       Resource id published by gcloud_token (gcloud_token_payload_t*).
 * @param max_messages   Maximum number of messages per pull.
 * @param output_interface Output interface for the response.
 */
bool curl_event_plugin_pubsub_pull_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    int max_messages,
    curl_sink_interface_t *output_interface
);

/**
 * Initialize the Pub/Sub acknowledgment plugin.
 *
 * @param loop           The curl_event_loop instance.
 * @param project_id     The Google Cloud project ID.
 * @param subscription_id The Pub/Sub subscription ID (or full path).
 * @param token_id       Resource id published by gcloud_token (gcloud_token_payload_t*).
 * @param ack_ids        Array of ack IDs to acknowledge.
 * @param num_ack_ids    Number of ack IDs.
 */
bool curl_event_plugin_pubsub_ack_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char **ack_ids,
    size_t num_ack_ids
);

bool curl_event_plugin_pubsub_seek_to_timestamp_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *timestamp  /* RFC3339 */
);

bool curl_event_plugin_pubsub_seek_to_snapshot_init(
    curl_event_loop_t *loop,
    const char *project_id,
    const char *subscription_id,
    curl_event_res_id  token_id,
    const char *snapshot
);

#ifdef __cplusplus
}
#endif

#endif // A_CURL_GCLOUD_PLUGIN_PUBSUB_PULL_H
