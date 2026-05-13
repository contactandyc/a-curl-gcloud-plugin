# a-curl-gcloud-plugin

`a-curl-gcloud-plugin` is a specialized extension for `a-curl-library` that provides native, asynchronous C bindings for Google Cloud Platform (GCP) REST APIs.

Instead of manually constructing JSON payloads, handling OAuth 2.0 JWT exchanges, and parsing deeply nested JSON responses, this library wraps those operations into memory-safe C functions that plug directly into the `a-curl-library` event loop.

## Architecture & Features (Why use it)

GCP APIs are notoriously strict regarding authentication token lifecycles, JSON schemas, and rate limits. This library abstracts those requirements using the underlying host library's mechanics.

* **Dependency-Driven Authentication:** Authentication is handled via the `curl_resource` API. You initialize a token manager once, and it publishes an OAuth `access_token` to the event loop. API requests declare a dependency on this token. If the token expires, the event loop automatically pauses your API requests, refreshes the token via Google's servers, and resumes your requests without dropping any data.
* **Automatic Rate Limiting:** GCP APIs have strict quotas. This plugin registers exact rate limits for services (e.g., Vertex AI Embeddings at 50 requests/24.5 RPS, Google Vision at 5 requests/10.0 RPS). The `a-curl-library` token bucket scheduler ensures your application stays within bounds and automatically applies jittered backoffs if an HTTP 429 occurs.
* **Zero-Boilerplate Serialization:** The `plugins/` directory contains functions that automatically build properly formatted JSON request bodies (using `ajson_t`) for complex endpoints like Pub/Sub publish arrays or Vertex AI prediction instances.
* **Type-Safe Response Parsing:** The `sinks/` directory contains specialized callbacks that intercept HTTP responses. They automatically parse the JSON, decode `Base64WebSafe` payloads (crucial for Pub/Sub and Gmail), extract HTML into plain text (for Gmail bodies), and hand your application clean C-structures (like `gcloud_v1_gmail_message_t`). All memory is tied to the request's memory pool and freed automatically.

## Supported APIs

The plugin currently provides bindings and sinks for the following Google services:

* **Authentication:** Service Account Key JSON to OAuth 2.0 (JWT) and Compute Engine Metadata Server tokens.
* **Vertex AI (Embeddings):** Text embeddings generation with dimensionality control and token statistics.
* **Pub/Sub (v1):** Topics and Subscriptions (CRUD, list), Publishing (with attributes and ordering keys), Pulling, Acknowledging, Seeking, and Schema management (AVRO/Protobuf).
* **Gmail (v1):** Message/Attachment retrieval, HTML-to-text extraction, message sending (raw MIME), trashing, deleting, and searching.
* **Cloud Spanner:** Session creation and SQL query execution.
* **Cloud SQL:** Direct query execution.
* **Google Cloud Storage (GCS):** Object downloading.
* **Google Vision:** Image annotation and Web Detection.
* **Google Custom Search:** Search engine querying.

## Usage (How to use it)

Because this is a plugin, it requires a running `curl_event_loop_t` from `a-curl-library`.

### 1. Initialize Authentication

You must create a token resource. This resource will automatically read your Service Account JSON, negotiate an access token, and make it available to the rest of the event loop.

```c
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-gcloud-plugin/plugins/token.h"

// 1. Declare a unique ID for the token in the event loop
curl_event_res_id token_id = curl_event_res_declare(loop);

// 2. Initialize the token plugin. 
// It will look for "service_account.json" in the current directory or the default gcloud config path.
// The `true` parameter tells the loop to automatically refresh the token before it expires.
curl_event_request_t *auth_req = curl_event_plugin_gcloud_token_init(
    loop, "service_account.json", token_id, true
);

```

### 2. Make an API Request (e.g., Vertex AI Embeddings)

Once the token is declared, you can queue API requests immediately. The event loop will automatically block this request until the token from Step 1 is fetched and published.

```c
#include "a-curl-gcloud-plugin/plugins/v1/embeddings.h"
#include "a-curl-gcloud-plugin/sinks/v1/embeddings.h"

// The callback that receives the parsed embeddings
void on_embeddings_ready(void *arg, curl_event_request_t *req, bool success, 
                         float **embeddings, size_t num_embeddings, size_t dim) {
    if (success) {
        printf("Received %zu embeddings of dimension %zu\n", num_embeddings, dim);
        // Process embeddings[0][0] to embeddings[0][dim-1]
    }
}

void fetch_embeddings(curl_event_loop_t *loop, curl_event_res_id token_id) {
    const char *texts[] = {"Hello world", "Machine learning in C"};

    // 1. Initialize the request builder for Vertex AI
    curl_event_request_t *req = gcloud_v1_embeddings_init_region(
        loop, token_id, "my-gcp-project", "us-central1", "text-embedding-004"
    );

    // 2. Add the inputs
    gcloud_v1_embeddings_add_texts(req, texts, 2);
    gcloud_v1_embeddings_set_output_dimensionality(req, 256);

    // 3. Attach the specific sink to parse the Google JSON response into float arrays
    gcloud_v1_embeddings_sink(req, 256, on_embeddings_ready, NULL);

    // 4. Submit to the loop
    gcloud_v1_embeddings_submit(loop, req, 0);
}

```

### 3. Fetching and Decoding Complex Data (e.g., Pub/Sub Pull)

APIs like Pub/Sub return data in `Base64WebSafe` format. The sink interface handles decoding this into standard byte arrays for you.

```c
#include "a-curl-gcloud-plugin/plugins/v1/pubsub.h"
#include "a-curl-gcloud-plugin/sinks/v1/pubsub.h"

void on_pubsub_pull(void *arg, curl_event_request_t *req, bool success, 
                    const gcloud_v1_pubsub_received_message_t *msgs, size_t num_msgs) {
    if (success) {
        for (size_t i = 0; i < num_msgs; i++) {
            // msgs[i].data is already base64-decoded
            printf("Message ID: %s\n", msgs[i].message_id);
            printf("Payload: %.*s\n", (int)msgs[i].data_len, msgs[i].data);
            
            // To prevent receiving this again, you would pass msgs[i].ack_id 
            // to gcloud_v1_pubsub_subscriptions_ack_init()
        }
    }
}

void pull_messages(curl_event_loop_t *loop, curl_event_res_id token_id) {
    // 1. Build the Pull request
    curl_event_request_t *req = gcloud_v1_pubsub_subscriptions_pull_init(
        loop, token_id, gcloud_v1_pubsub_endpoint_global(), 
        "my-gcp-project", "my-subscription"
    );
    
    gcloud_v1_pubsub_subscriptions_pull_set_max_messages(req, 10);

    // 2. Attach the pull sink to decode the envelope and base64 payloads
    gcloud_v1_pubsub_pull_sink(req, on_pubsub_pull, NULL);

    // 3. Submit
    gcloud_v1_pubsub_submit(loop, req, 0);
}

```

## Internal Module Layout

To understand the codebase, note the strict separation of concerns between `plugins` and `sinks`:

* **`include/a-curl-gcloud-plugin/plugins/` & `src/plugins/**`: These files act as **Request Builders**. They allocate the URL, define the HTTP method, construct the JSON `post_data`, configure retries/timeouts, and attach the authorization headers via the `on_prepare` callback.
* **`include/a-curl-gcloud-plugin/sinks/` & `src/sinks/**`: These files act as **Response Parsers**. They implement `curl_sink_interface_t`. As `libcurl` streams bytes from the network, these files buffer the data, parse the JSON, extract base64, and pack the results into C-structs before handing them to your application callbacks.
