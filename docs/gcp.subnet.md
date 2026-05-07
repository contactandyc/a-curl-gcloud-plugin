Awesome — let’s keep building. Here’s an **implementation‑ready Subnet plugin** that pairs cleanly with the VPC plugin you approved. I also included a compact **Cloud NAT plugin** right after, since most stacks wire Subnets → Router → NAT.

---

## Plugin: `gcp.subnet`

### What it manages

* **Subnetworks (regional)**: create / get / list / delete
* **Primary CIDR expansion** (one‑way grow)
* **Secondary IP ranges** (for GKE Pods/Services, etc.)
* **Private Google Access (PGA)** on/off
* **VPC Flow Logs** on/off + log config
* (Optional) **Dual‑stack** (IPv4+IPv6) where supported

> **Not here:** firewall rules (you can keep those in a dedicated `gcp.firewall`), routers/NAT (see `gcp.cloudnat` below).

### Auth & Base

* **Auth**: `Authorization: Bearer <token>`
* **Base**: `https://compute.googleapis.com/compute/v1`
* **Scope**: regional resource
  `/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNETWORK}`
* **LROs**: regional operations at
  `/projects/{PROJECT}/regions/{REGION}/operations/{OP}`

### Recommended interface (high‑level)

```yaml
gcp.subnet:
  createSubnetwork(
    name, region, network, ipCidrRange,
    secondaryIpRanges?,               # [{rangeName, ipCidrRange}]
    privateIpGoogleAccess?,           # bool
    enableFlowLogs?,                  # bool
    logConfig?,                       # {flowSampling, aggregationInterval, metadata, metadataFields?}
    stackType?                        # "IPV4_ONLY" | "IPV4_IPV6"
  )
  getSubnetwork(name, region)
  listSubnetworks(region?)            # if region omitted, use aggregated list
  deleteSubnetwork(name, region)

  expandPrimaryCidr(name, region, newIpCidrRange)

  setPrivateGoogleAccess(name, region, enabled)   # convenience wrapper

  addSecondaryRanges(name, region, ranges[])      # merge-add
  removeSecondaryRanges(name, region, rangeNames[])

  updateFlowLogs(name, region, enabled, logConfig?)
```

---

### Wire‑level API calls

#### 1) Create a subnetwork

**HTTP**

```
POST https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks?requestId={UUID}
```

**Body (minimal)**

```json
{
  "name": "subnet-us-central1-a",
  "network": "projects/PROJECT_ID/global/networks/vpc-main",
  "ipCidrRange": "10.10.0.0/20"
}
```

**Body (GKE‑ready: secondary ranges + flow logs + PGA)**

```json
{
  "name": "subnet-uc1-app",
  "network": "projects/PROJECT_ID/global/networks/vpc-main",
  "ipCidrRange": "10.20.0.0/20",
  "secondaryIpRanges": [
    { "rangeName": "gke-pods",     "ipCidrRange": "10.21.0.0/14" },
    { "rangeName": "gke-services", "ipCidrRange": "10.26.0.0/20" }
  ],
  "privateIpGoogleAccess": true,
  "enableFlowLogs": true,
  "logConfig": {
    "aggregationInterval": "INTERVAL_5_MIN",
    "flowSampling": 0.5,
    "metadata": "INCLUDE_ALL_METADATA"
  }
}
```

> **Notes**
>
> * `network` must be a full self‑link or `projects/{id}/global/networks/{name}`.
> * Names: 1–63 chars, lower‑case, letters/digits/`-`, start with a letter; **unique within project**.
> * Primary CIDR **cannot** overlap existing subnets in the same VPC.
> * Secondary ranges must be unique `rangeName`s within the subnet and non‑overlapping.

#### 2) Get / List / Aggregated list / Delete

**Get**

```
GET https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}
```

**List (by region)**

```
GET https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks
```

**Aggregated list (all regions)**

```
GET https://compute.googleapis.com/compute/v1/projects/{PROJECT}/aggregated/subnetworks
```

**Delete**

```
DELETE https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}?requestId={UUID}
```

> **Delete caveat:** Subnet must have **no dependent resources** (VM NICs, GKE node pools/clusters using its ranges, Serverless VPC connectors, private service connect endpoints, routers/NAT scoped to it when “list of subnets” is configured, etc.).

#### 3) Expand the **primary** CIDR (grow only)

```
POST https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}/expandIpCidrRange?requestId={UUID}
```

**Body**

```json
{ "ipCidrRange": "10.20.0.0/19" }
```

> Only expansion is allowed; cannot shrink or change base. Ensure no overlap.

#### 4) Toggle **Private Google Access**

Option A (dedicated method):

```
POST https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}/setPrivateIpGoogleAccess?requestId={UUID}
```

**Body**

```json
{ "privateIpGoogleAccess": true }
```

Option B (via `PATCH`, useful if you’re also updating logs):

```
PATCH https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}?requestId={UUID}
```

```json
{ "privateIpGoogleAccess": true }
```

#### 5) Enable/Configure **VPC Flow Logs**

```
PATCH https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/subnetworks/{SUBNET}?requestId={UUID}
```

**Body (examples)**

Enable with defaults:

```json
{ "enableFlowLogs": true }
```

Custom config:

```json
{
  "enableFlowLogs": true,
  "logConfig": {
    "aggregationInterval": "INTERVAL_5_MIN",
    "flowSampling": 0.2,
    "metadata": "CUSTOM_METADATA",
    "metadataFields": ["connection", "src_instance", "dest_location"]
  }
}
```

Disable:

```json
{ "enableFlowLogs": false }
```

#### 6) Manage **secondary IP ranges** (add/remove)

There is no dedicated “add/remove” RPC; use `PATCH` and send the **updated full list** of `secondaryIpRanges`.

**Add**

1. GET current subnet to read existing `secondaryIpRanges`
2. Merge your additions
3. PATCH with the full array

```json
{
  "secondaryIpRanges": [
    { "rangeName": "gke-pods",     "ipCidrRange": "10.21.0.0/14" },
    { "rangeName": "gke-services", "ipCidrRange": "10.26.0.0/20" },
    { "rangeName": "data-jobs",    "ipCidrRange": "10.27.0.0/22" }
  ]
}
```

**Remove**
Send the list **minus** the ranges you want to drop.

> **Cautions**
>
> * Some fields are **immutable** after creation: region, network, primary `ipCidrRange` base.
> * Don’t drop ranges still referenced by GKE clusters, NEG backends, or connectors.
> * For GKE, your cluster will reference `rangeName`s in its `ipAllocationPolicy`.

---

### Example: end‑to‑end cURL

```bash
TOKEN="$(gcloud auth print-access-token)"
BASE="https://compute.googleapis.com/compute/v1"
PROJECT="my-proj"
REGION="us-central1"
REQID="$(uuidgen)"

# Create subnet (custom VPC assumed)
curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X POST "$BASE/projects/$PROJECT/regions/$REGION/subnetworks?requestId=$REQID" \
  -d '{
        "name": "subnet-uc1-app",
        "network": "projects/my-proj/global/networks/vpc-main",
        "ipCidrRange": "10.20.0.0/20",
        "secondaryIpRanges": [
          {"rangeName":"gke-pods","ipCidrRange":"10.21.0.0/14"},
          {"rangeName":"gke-svcs","ipCidrRange":"10.26.0.0/20"}
        ],
        "privateIpGoogleAccess": true,
        "enableFlowLogs": true,
        "logConfig": { "aggregationInterval":"INTERVAL_5_MIN","flowSampling":0.5,"metadata":"INCLUDE_ALL_METADATA"}
      }'
# -> poll regional operation until DONE
```

---

### Permissions you’ll need

* **roles/compute.networkAdmin** (covers subnetworks, routers, routes, NAT)
* For flow logs to Cloud Logging, the project/service must have logging enabled; IAM on logs is usually covered by standard roles.

### Validation your plugin should do

* CIDR syntax, overlap checks (client‑side preflight helps UX).
* Range names: `^[a-z]([-a-z0-9]{0,61}[a-z0-9])?$`
* Ensure `network` exists and is **custom‑mode** VPC (your VPC plugin set that).
* When removing secondary ranges, verify nothing references them.

### Common errors

* `INVALID_FIELD_VALUE` (bad CIDR or overlapping ranges)
* `RESOURCE_IN_USE_BY_ANOTHER_RESOURCE` on delete or removing secondary ranges
* `PERMISSION_DENIED` (principal lacks network admin)

---

## Plugin: `gcp.cloudnat`

Most deployments attach Cloud NAT to let private workloads egress the internet. Cloud NAT is configured **inside a Cloud Router**.

### What it manages

* **Router** (create/get/list/delete)
* **NAT** blocks within a Router (create/update/delete)
* IP allocation: **AUTO\_ONLY** or **MANUAL\_ONLY** (with regional static addresses)
* Subnet selection: **all** subnets or **specific** subnets / ranges
* NAT logging, timeouts, min ports per VM, endpoint‑independent mapping (EIM)

### Core resources & endpoints

* **Router (regional)**
  `/projects/{PROJECT}/regions/{REGION}/routers/{ROUTER}`
* **Addresses (regional)** (only if `MANUAL_ONLY`)
  `/projects/{PROJECT}/regions/{REGION}/addresses`
* **Ops**: `/projects/{PROJECT}/regions/{REGION}/operations/{OP}`

### Suggested interface

```yaml
gcp.cloudnat:
  ensureRouter(name, region, network)        # create if absent
  getRouter(name, region)
  listRouters(region)

  upsertNat(
    router, region, natName,
    ipAllocate: { mode: "AUTO_ONLY" | "MANUAL_ONLY", addresses?: [addressSelfLinkOrName] },
    selection:
      { type: "ALL_SUBNETWORKS_ALL_IP_RANGES" |
              "ALL_SUBNETWORKS_ALL_PRIMARY_IP_RANGES" |
              "LIST_OF_SUBNETWORKS",
        subnets?: [ { subnetwork: string, sourceIpRangesToNat: ["ALL_IP_RANGES"|"PRIMARY_IP_RANGE"|"LIST_OF_SECONDARY_IP_RANGES"], secondaryIpRangeNames?: [string] } ]
      },
    eimEnabled?: true,
    minPortsPerVm?: 128,
    timeouts?: { tcpEstablishedSec?, tcpTransitorySec?, udpSec?, icmpSec? },
    logConfig?: { enable: true, filter: "ERRORS_ONLY" | "TRANSLATIONS_ONLY" | "ALL" }
  )

  deleteNat(router, region, natName)
  deleteRouter(name, region)                  # only if no NATs remain
```

### Wire‑level API calls

#### 1) Create (or ensure) a **Router**

```
POST https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/routers?requestId={UUID}
```

**Body**

```json
{
  "name": "rtr-uc1",
  "network": "projects/PROJECT_ID/global/networks/vpc-main"
}
```

(If it exists, skip creation.)

#### 2) Create/Update a **NAT** on the Router

> NATs live in the Router’s `nats` array. Use `GET` to read, merge your NAT definition, then `PATCH` the Router.

**Get the router**

```
GET https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/routers/{ROUTER}
```

**Patch with NAT config**

```
PATCH https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/routers/{ROUTER}?requestId={UUID}
```

**Body (common “all subnets, auto IPs”)**

```json
{
  "nats": [
    {
      "name": "nat-main",
      "natIpAllocateOption": "AUTO_ONLY",
      "sourceSubnetworkIpRangesToNat": "ALL_SUBNETWORKS_ALL_IP_RANGES",
      "enableEndpointIndependentMapping": true,
      "minPortsPerVm": 128,
      "logConfig": { "enable": true, "filter": "ERRORS_ONLY" }
    }
    /* include any existing NATs here as well */
  ]
}
```

**Body (manual IPs + selected subnets/ranges)**

```json
{
  "nats": [
    {
      "name": "nat-main",
      "natIpAllocateOption": "MANUAL_ONLY",
      "natIps": [
        "projects/PROJECT/regions/us-central1/addresses/nat-ip-1",
        "projects/PROJECT/regions/us-central1/addresses/nat-ip-2"
      ],
      "sourceSubnetworkIpRangesToNat": "LIST_OF_SUBNETWORKS",
      "subnetworks": [
        {
          "name": "projects/PROJECT/regions/us-central1/subnetworks/subnet-uc1-app",
          "sourceIpRangesToNat": ["PRIMARY_IP_RANGE","LIST_OF_SECONDARY_IP_RANGES"],
          "secondaryIpRangeNames": ["gke-pods","gke-svcs"]
        }
      ],
      "enableEndpointIndependentMapping": true,
      "logConfig": { "enable": true, "filter": "TRANSLATIONS_ONLY" }
    }
  ]
}
```

> **Important:** `PATCH` **replaces** the router’s `nats` array. Always merge with any existing NATs to avoid deleting them unintentionally.

#### 3) Delete a **NAT**

Read the router, remove the NAT from the `nats` array, then `PATCH` with the remaining NATs.

#### 4) Delete the **Router**

```
DELETE https://compute.googleapis.com/compute/v1/projects/{PROJECT}/regions/{REGION}/routers/{ROUTER}?requestId={UUID}
```

> Only works if **no NATs** (and no BGP peers) remain.

---

### Example: NAT quick start (cURL)

```bash
TOKEN="$(gcloud auth print-access-token)"
BASE="https://compute.googleapis.com/compute/v1"
PROJECT="my-proj"
REGION="us-central1"
REQID="$(uuidgen)"

# Ensure Router
curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X POST "$BASE/projects/$PROJECT/regions/$REGION/routers?requestId=$REQID" \
  -d '{
        "name": "rtr-uc1",
        "network": "projects/my-proj/global/networks/vpc-main"
      }'

# Upsert NAT (auto IPs, all subnets)
curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X PATCH "$BASE/projects/$PROJECT/regions/$REGION/routers/rtr-uc1?requestId=$REQID" \
  -d '{
        "nats": [{
          "name": "nat-main",
          "natIpAllocateOption": "AUTO_ONLY",
          "sourceSubnetworkIpRangesToNat": "ALL_SUBNETWORKS_ALL_IP_RANGES",
          "enableEndpointIndependentMapping": true,
          "minPortsPerVm": 128,
          "logConfig": {"enable": true, "filter": "ERRORS_ONLY"}
        }]
      }'
# -> poll regional operation until DONE
```

---

## How these compose

* **VPC** (`gcp.vpc`) → create **custom‑mode** network.
* **Subnet** (`gcp.subnet`) → create per‑region subnets; add **secondary ranges** for GKE (Pods/Services); enable **PGA** and **flow logs** as needed.
* **Cloud NAT** (`gcp.cloudnat`) → create **Router**, then **NAT** for egress from those subnets.
* **GKE plugin** will reference: `network: vpc-main`, `subnetwork: subnet-uc1-app`, and your `secondaryIpRangeNames`.
* **Cloud Run plugin** will usually provision a **Serverless VPC Access connector** into a subnet; for public egress, NAT often required.

---

## Guardrails & idempotency

* Use `?requestId=<uuid>` on all mutating calls (insert/patch/delete/expand) to make retries safe.
* Poll the corresponding **regional operation** until `status: DONE`, then surface errors if present.
* Preflight checks on overlapping CIDRs and resource existence make the UX predictable.

---

If you’re happy with this, I can proceed with **GKE** (cluster + nodepool API shapes wired to your subnet secondary ranges) and then **Cloud Run** (service + VPC connector) — both designed to plug into the VPC/Subnet/NAT you now have.
