Fantastic — let’s wire up the next two building blocks: **GKE** and **Cloud Run**. I’m keeping the shape consistent with your VPC/Subnet/NAT plugins so these compose cleanly.

---

## Plugin: `gcp.gke`

### What it manages

* **Clusters (regional or zonal)**: create / get / list / delete
* **Node pools** (for Standard clusters): create / get / list / delete
* **Cluster updates**: logging/monitoring, master authorized networks, maintenance windows, release channels, private clusters, Workload Identity, network dataplane, etc.

> **Networking alignment:** This plugin expects your existing **VPC** and **Subnet**. For **GKE Standard**, pass the **secondary range names** you created in `gcp.subnet` (`gke-pods`, `gke-svcs`) via `ipAllocationPolicy`.

### API, auth, LROs

* **Base**: `https://container.googleapis.com/v1`
* **Auth**: `Authorization: Bearer <token>`
* **Clusters**: `/projects/{PROJECT}/locations/{LOCATION}/clusters`
* **Node pools**: `/projects/{PROJECT}/locations/{LOCATION}/clusters/{CLUSTER}/nodePools`
* **LROs**: `/projects/{PROJECT}/locations/{LOCATION}/operations/{OP}`

> `LOCATION` can be a **region** (recommended) or **zone**.

### Recommended high‑level interface

```yaml
gcp.gke:
  # Clusters
  createClusterStandard(
    name, location, network, subnetwork,
    releaseChannel?,                        # "RAPID"|"REGULAR"|"STABLE"
    ipAllocationPolicy: {
      useIpAliases: true,
      clusterSecondaryRangeName,            # e.g., "gke-pods"
      servicesSecondaryRangeName,           # e.g., "gke-svcs"
      stackType?                            # "IPV4_ONLY"|"IPV4_IPV6"
    },
    privateCluster?: {
      enablePrivateNodes: true|false,
      enablePrivateEndpoint?: false,
      masterIpv4CidrBlock?: "172.16.0.0/28",
      masterAuthorizedNetworks?: [{ cidr, displayName? }]
    },
    workloadIdentity?: { workloadPool },    # e.g., "PROJECT_ID.svc.id.goog"
    shieldedNodes?: true,
    logging?: { components?: ["SYSTEM_COMPONENTS","WORKLOADS"] },
    monitoring?: { managedPrometheus?: true },
    networkDataplane?: "ADVANCED_DATAPATH"|"DATAPATH_PROVIDER_UNSPECIFIED",
    maintenanceWindow?                      # RFC3339 or recurring
  )

  createClusterAutopilot(
    name, location, network, subnetwork,
    releaseChannel?, workloadIdentity?      # Autopilot has no user nodepools
  )

  getCluster(name, location)
  listClusters(location?)
  deleteCluster(name, location)

  # Node Pools (Standard only)
  createNodePool(
    cluster, location, name,
    size?: { initial?: 1 },
    autoscaling?: { enabled: true, min: 1, max: 5 },
    config: {
      machineType: "e2-standard-4",
      diskType?: "pd-standard"|"pd-balanced"|"pd-ssd",
      diskSizeGb?: 100,
      imageType?: "COS_CONTAINERD",
      serviceAccount?: "gke-nodes@PROJECT_ID.iam.gserviceaccount.com",
      preemptible?: false, spot?: false,
      labels?: { ... }, tags?: [ ... ],
      taints?: [{ key, value, effect }],    # effect: "NO_SCHEDULE"|...
      shieldedInstanceConfig?: { enableSecureBoot?: true }
    },
    management?: { autoUpgrade?: true, autoRepair?: true },
    version?: "1.29.x-gke.*"                # optional; else default/channel
  )

  getNodePool(cluster, location, name)
  listNodePools(cluster, location)
  deleteNodePool(cluster, location, name)
```

### Wire‑level calls (REST)

#### Create **Standard** cluster (with IP aliases & named secondary ranges)

```
POST https://container.googleapis.com/v1/projects/{PROJECT}/locations/{LOCATION}/clusters
```

**Body**

```json
{
  "cluster": {
    "name": "gke-app",
    "network": "projects/PROJECT_ID/global/networks/vpc-main",
    "subnetwork": "projects/PROJECT_ID/regions/us-central1/subnetworks/subnet-uc1-app",
    "releaseChannel": { "channel": "REGULAR" },
    "ipAllocationPolicy": {
      "useIpAliases": true,
      "clusterSecondaryRangeName": "gke-pods",
      "servicesSecondaryRangeName": "gke-svcs",
      "stackType": "IPV4_ONLY"
    },
    "workloadIdentityConfig": { "workloadPool": "PROJECT_ID.svc.id.goog" },
    "shieldedNodes": { "enabled": true },
    "loggingConfig": {
      "componentConfig": { "enableComponents": ["SYSTEM_COMPONENTS","WORKLOADS"] }
    },
    "monitoringConfig": { "managedPrometheus": { "enabled": true } },
    "networkConfig": { "datapathProvider": "ADVANCED_DATAPATH" }
  }
}
```

> If you want **private cluster**:

```json
"privateClusterConfig": {
  "enablePrivateNodes": true,
  "enablePrivateEndpoint": false,
  "masterIpv4CidrBlock": "172.16.0.0/28"
},
"masterAuthorizedNetworksConfig": {
  "enabled": true,
  "cidrBlocks": [
    { "cidrBlock": "203.0.113.0/24", "displayName": "corp" }
  ]
}
```

#### Create **Autopilot** cluster

Same endpoint; supply the `autopilot` block:

```json
{
  "cluster": {
    "name": "gke-auto",
    "network": "projects/PROJECT_ID/global/networks/vpc-main",
    "subnetwork": "projects/PROJECT_ID/regions/us-central1/subnetworks/subnet-uc1-app",
    "releaseChannel": { "channel": "REGULAR" },
    "autopilot": { "enabled": true },
    "workloadIdentityConfig": { "workloadPool": "PROJECT_ID.svc.id.goog" }
  }
}
```

> Autopilot manages node pools for you; do **not** call `createNodePool` for Autopilot.

#### Create a **Node Pool** (Standard)

```
POST https://container.googleapis.com/v1/projects/{PROJECT}/locations/{LOCATION}/clusters/{CLUSTER}/nodePools
```

**Body**

```json
{
  "nodePool": {
    "name": "pool-default",
    "initialNodeCount": 1,
    "config": {
      "machineType": "e2-standard-4",
      "diskType": "pd-balanced",
      "diskSizeGb": 100,
      "imageType": "COS_CONTAINERD",
      "serviceAccount": "gke-nodes@PROJECT_ID.iam.gserviceaccount.com",
      "shieldedInstanceConfig": { "enableSecureBoot": true }
    },
    "autoscaling": { "enabled": true, "minNodeCount": 1, "maxNodeCount": 5 },
    "management": { "autoUpgrade": true, "autoRepair": true }
  }
}
```

#### Get / list / delete

```
GET    .../projects/{PROJECT}/locations/{LOCATION}/clusters/{CLUSTER}
GET    .../projects/{PROJECT}/locations/{LOCATION}/clusters
DELETE .../projects/{PROJECT}/locations/{LOCATION}/clusters/{CLUSTER}
```

Node pools:

```
GET    .../clusters/{CLUSTER}/nodePools
GET    .../clusters/{CLUSTER}/nodePools/{POOL}
DELETE .../clusters/{CLUSTER}/nodePools/{POOL}
```

### Example: end‑to‑end (Standard + named ranges)

```bash
TOKEN="$(gcloud auth print-access-token)"
BASE="https://container.googleapis.com/v1"
PROJECT="my-proj"
LOC="us-central1"

# Create cluster
curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X POST "$BASE/projects/$PROJECT/locations/$LOC/clusters" \
  -d '{
        "cluster": {
          "name": "gke-app",
          "network": "projects/my-proj/global/networks/vpc-main",
          "subnetwork": "projects/my-proj/regions/us-central1/subnetworks/subnet-uc1-app",
          "releaseChannel": { "channel": "REGULAR" },
          "ipAllocationPolicy": {
            "useIpAliases": true,
            "clusterSecondaryRangeName": "gke-pods",
            "servicesSecondaryRangeName": "gke-svcs"
          },
          "workloadIdentityConfig": { "workloadPool": "my-proj.svc.id.goog" },
          "shieldedNodes": { "enabled": true }
        }
      }'

# Create a node pool
curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X POST "$BASE/projects/$PROJECT/locations/$LOC/clusters/gke-app/nodePools" \
  -d '{
        "nodePool": {
          "name": "pool-default",
          "initialNodeCount": 1,
          "config": { "machineType": "e2-standard-4", "imageType": "COS_CONTAINERD" },
          "autoscaling": { "enabled": true, "minNodeCount": 1, "maxNodeCount": 5 }
        }
      }'
```

### IAM required (typical)

* **roles/container.admin**
* **roles/compute.networkAdmin** (to use custom VPC/subnet)
* **roles/iam.serviceAccountUser** on the node SA (if customizing)
* Optionally **roles/monitoring.admin**/**logging.admin** for advanced configs

### Guardrails

* Enforce **named secondary ranges** existence before create (Standard).
* Validate cluster name (`^[a-z]([-a-z0-9]{0,38}[a-z0-9])?$`).
* If `privateClusterConfig.enablePrivateNodes=true`, ensure **NAT** is present for egress.
* Autopilot: block `createNodePool` and advanced node config to avoid invalid requests.
* Always poll the LRO until `DONE`, surface `error` if present.

---

## Plugin: `gcp.cloudrun`

### What it manages

* **Services** (v2): create / get / list / delete / update traffic
* **IAM policy** for invokers (public/limited)
* **VPC connectivity** via **Serverless VPC Access connector** (optional; see micro‑plugin below)
* **Revisions** are managed implicitly via `template` changes and `traffic` splits

### API, auth, LROs

* **Base**: `https://run.googleapis.com/v2`
* **Auth**: `Authorization: Bearer <token>`
* **Services**: `/projects/{PROJECT}/locations/{REGION}/services/{SERVICE}`
* **IAM**: `/projects/{PROJECT}/locations/{REGION}/services/{SERVICE}:setIamPolicy`
* **LROs**: `/projects/{PROJECT}/locations/{REGION}/operations/{OP}`

### Recommended high‑level interface

```yaml
gcp.cloudrun:
  deployService(
    name, region,
    image,                              # gcr.io/... or artifactregistry image
    containerPort?: 8080,
    env?: [{ name, value } | { name, secret: { name, version } }],
    serviceAccount?: "svc-foo@PROJECT.iam.gserviceaccount.com",
    concurrency?: 80,                   # containerConcurrency
    scaling?: { min?: 0, max?: 100 },   # minInstanceCount / maxInstanceCount
    cpu?: "1", memory?: "512Mi",        # per-container resources
    ingress?: "INGRESS_TRAFFIC_ALL" | "INGRESS_TRAFFIC_INTERNAL_ONLY" | "INGRESS_TRAFFIC_INTERNAL_LOAD_BALANCER",
    vpcAccess?: {                       # optional (requires connector)
      connector: "projects/PROJECT/locations/REGION/connectors/NAME",
      egress: "PRIVATE_RANGES_ONLY" | "ALL_TRAFFIC"
    },
    executionEnvironment?: "EXECUTION_ENVIRONMENT_GEN2",
    traffic?: [ { latest: true, percent: 100 } | { revision, percent } ]
  )

  getService(name, region)
  listServices(region)
  deleteService(name, region)

  setIamPolicyInvoker(name, region, members)  # e.g., ["allUsers"] or ["user:you@x.com"]
  splitTraffic(name, region, traffic[])        # blue/green or canary
```

### Wire‑level calls (REST)

#### Deploy / Update a Service (idempotent upsert)

```
POST https://run.googleapis.com/v2/projects/{PROJECT}/locations/{REGION}/services
# or PATCH https://run.googleapis.com/v2/projects/{PROJECT}/locations/{REGION}/services/{SERVICE} with updateMask
```

**Body (create)**

```json
{
  "service": {
    "name": "projects/PROJECT/locations/us-central1/services/hello",
    "ingress": "INGRESS_TRAFFIC_ALL",
    "template": {
      "containers": [{
        "image": "us-docker.pkg.dev/PROJECT/app/hello:1.0.0",
        "ports": [{ "containerPort": 8080 }],
        "env": [
          { "name": "GREETING", "value": "hi" },
          { "name": "SECRET_API_KEY", "valueSource": {
              "secretRef": { "name": "api-key", "version": "latest" }
          } }
        ],
        "resources": { "cpuIdle": true, "limits": { "cpu": "1", "memory": "512Mi" } }
      }],
      "serviceAccount": "svc-hello@PROJECT.iam.gserviceaccount.com",
      "containerConcurrency": 80,
      "scaling": { "minInstanceCount": 0, "maxInstanceCount": 20 },
      "vpcAccess": {
        "connector": "projects/PROJECT/locations/us-central1/connectors/run-conn",
        "egress": "PRIVATE_RANGES_ONLY"
      },
      "executionEnvironment": "EXECUTION_ENVIRONMENT_GEN2"
    },
    "traffic": [{ "type": "TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST", "percent": 100 }]
  }
}
```

#### Public or limited access (IAM)

```
POST https://run.googleapis.com/v2/projects/{PROJECT}/locations/{REGION}/services/{SERVICE}:setIamPolicy
```

**Make public**

```json
{
  "policy": {
    "bindings": [
      { "role": "roles/run.invoker", "members": ["allUsers"] }
    ]
  }
}
```

**Restrict to specific members**

```json
{ "policy": { "bindings": [
  { "role": "roles/run.invoker", "members": ["user:alice@example.com","group:eng@example.com"] }
]}}
```

#### Blue/green or canary split

Use `PATCH` with `updateMask=traffic`:

```
PATCH https://run.googleapis.com/v2/projects/{PROJECT}/locations/{REGION}/services/{SERVICE}?updateMask=traffic
```

**Example**

```json
{
  "service": {
    "traffic": [
      { "type": "TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST", "percent": 10 },
      { "type": "TRAFFIC_TARGET_ALLOCATION_TYPE_REVISION",
        "revision": "hello-00013-abc", "percent": 90 }
    ]
  }
}
```

### Example: minimal cURL deployment

```bash
TOKEN="$(gcloud auth print-access-token)"
BASE="https://run.googleapis.com/v2"
PROJECT="my-proj"
REGION="us-central1"
SVC="hello"

curl -sS -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -X POST "$BASE/projects/$PROJECT/locations/$REGION/services" \
  -d "{
        \"service\": {
          \"name\": \"projects/$PROJECT/locations/$REGION/services/$SVC\",
          \"ingress\": \"INGRESS_TRAFFIC_ALL\",
          \"template\": {
            \"containers\": [{\"image\": \"us-docker.pkg.dev/$PROJECT/app/hello:1.0.0\"}],
            \"serviceAccount\": \"svc-hello@$PROJECT.iam.gserviceaccount.com\",
            \"containerConcurrency\": 80,
            \"scaling\": {\"minInstanceCount\": 0, \"maxInstanceCount\": 10}
          },
          \"traffic\": [{\"type\": \"TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST\", \"percent\": 100}]
        }
      }"
```

### IAM required (typical)

* **roles/run.admin**
* **roles/iam.serviceAccountUser** (for the service’s runtime SA)
* **roles/secretmanager.secretAccessor** (if mounting secrets)
* For VPC access: see `gcp.vpcaccess` below

### Guardrails

* Validate image URI format (gcr.io / Artifact Registry).
* For **private egress** needs, require a **VPC Access connector** and verify it exists (region must match).
* Enforce `name` and region constraints; keep `traffic` percentages summing to 100.
* Poll Run LROs until `DONE`, surface errors.

---

## Micro‑plugin (optional): `gcp.vpcaccess` (Serverless VPC Access)

Cloud Run (and Cloud Functions/Jobs) use **Serverless VPC Access** connectors to reach private IPs in your VPC.

### API & resources

* **Base**: `https://vpcaccess.googleapis.com/v1`
* **Connectors**: `/projects/{PROJECT}/locations/{REGION}/connectors/{NAME}`

> Best practice: dedicate a small subnet (for example `/28`) just for connectors to avoid IP contention.

### Interface

```yaml
gcp.vpcaccess:
  ensureConnector(name, region,
    network: "vpc-main",
    subnet?: { name: "svpc-us-central1" },     # preferred; pre-created subnet
    minThroughput?: 200, maxThroughput?: 300   # Mbps; optional
  )
  getConnector(name, region)
  deleteConnector(name, region)
```

### Create (REST)

```
POST https://vpcaccess.googleapis.com/v1/projects/{PROJECT}/locations/{REGION}/connectors
```

**Body**

```json
{
  "name": "projects/PROJECT/locations/us-central1/connectors/run-conn",
  "network": "vpc-main",
  "subnet": { "name": "svpc-us-central1" },
  "minThroughput": 200,
  "maxThroughput": 300
}
```

> The **subnet** must be in the same region and large enough for the connector (recommend a dedicated `/28`). Once created, reference this connector in `gcp.cloudrun.deployService(... vpcAccess.connector=...)`.

### IAM

* **roles/vpcaccess.admin**
* **roles/compute.networkAdmin**

---

## How these compose (end‑to‑end)

1. **VPC** (`gcp.vpc`) → custom‑mode network `vpc-main`.
2. **Subnet** (`gcp.subnet`) → `subnet-uc1-app` in `us-central1` with secondary ranges:

    * `gke-pods`: 10.21.0.0/14
    * `gke-svcs`: 10.26.0.0/20
3. **NAT** (`gcp.cloudnat`) → router `rtr-uc1`, NAT `nat-main` for all subnets.
4. **GKE** (`gcp.gke`) → Standard cluster `gke-app` using `vpc-main` + `subnet-uc1-app` and named ranges; create node pool(s).
5. **(Optional) VPC Access** (`gcp.vpcaccess`) → connector `run-conn` in `us-central1`.
6. **Cloud Run** (`gcp.cloudrun`) → service `hello` using image, runtime SA, (optional) `vpcAccess` to reach private DBs/services.

---

### Cross‑plugin validation you should enforce

* Subnet’s **secondary ranges** exist **before** creating GKE Standard cluster.
* **NAT** exists if cluster nodes are private (no public IPs).
* VPC Access **connector** region matches the Cloud Run service region.
* Names/URIs provided are **self‑links or canonical resource paths** as required by each API.

---

If you’d like, I can now provide:

* A **reference JSON Schema** for each plugin’s request/response types, or
* A **single “stack compose” spec** that instantiates VPC → Subnet → NAT → GKE → (VPC Access) → Cloud Run in one plan, with dependency ordering and output wiring (e.g., pass secondary range names and connector URI automatically).
