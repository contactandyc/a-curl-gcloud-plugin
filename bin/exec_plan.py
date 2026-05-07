#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
#
# Maintainer: Andy Curtis <contactandyc@gmail.com>

import argparse, json, os, re, subprocess, time
from urllib.parse import quote
from uuid import uuid4
import time
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

COMPUTE   = "https://compute.googleapis.com/compute/v1"
CONTAINER = "https://container.googleapis.com/v1"
RUN       = "https://run.googleapis.com/v2"
VPCACCESS = "https://vpcaccess.googleapis.com/v1"

SESSION = requests.Session()
_retry = Retry(
    total=6, connect=6, read=6,
    backoff_factor=0.8,
    status_forcelist=(429, 500, 502, 503, 504),
    allowed_methods=False,     # retry all methods
    raise_on_status=False,
)
_adapter = HTTPAdapter(max_retries=_retry, pool_connections=100, pool_maxsize=100)
SESSION.mount("https://", _adapter)
SESSION.mount("http://", _adapter)

# -------- auth / http --------
def token():
    env = os.getenv("GCP_TOKEN")
    if env: return env
    return subprocess.check_output(
        ["gcloud", "auth", "print-access-token"], text=True
    ).strip()

def hdrs():
    return {"Authorization": f"Bearer {token()}", "Content-Type": "application/json"}

def _req(method, url, body=None, ok=(200, 201, 202, 204)):
    # Explicit retries around TLS/connection failures that happen pre-request
    for attempt in range(6):
        try:
            r = SESSION.request(
                method, url,
                headers=hdrs(),
                data=None if body is None else json.dumps(body),
                timeout=(10, 120)  # (connect, read)
            )
            break
        except (requests.exceptions.SSLError,
                requests.exceptions.ConnectionError) as e:
            if attempt >= 5:
                raise
            time.sleep(0.8 * (attempt + 1))
            continue

    if r.status_code not in ok:
        try:
            msg = r.json()
        except Exception:
            msg = r.text
        raise RuntimeError(f"{method} {url} -> {r.status_code} {msg}")

    return None if r.status_code == 204 else (r.json() if r.text else {})

def get(url):   return _req("GET", url)
def post(url,b):return _req("POST",url,b)
def patch(url,b):return _req("PATCH",url,b)
def delete(url):return _req("DELETE",url)

# -------- LRO polling --------
def compute_wait(project, op_name, region=None, zone=None, poll=3):
    base = f"{COMPUTE}/projects/{project}"
    if region: url = f"{base}/regions/{region}/operations/{op_name}"
    elif zone: url = f"{base}/zones/{zone}/operations/{op_name}"
    else:      url = f"{base}/global/operations/{op_name}"
    while True:
        j = get(url)
        if j.get("status") == "DONE":
            if "error" in j: raise RuntimeError(j["error"])
            return j
        time.sleep(poll)

def container_wait(op_name, poll=5):
    url = f"{CONTAINER}/{op_name}"
    while True:
        j = get(url)
        if j.get("status") == "DONE":
            if "error" in j: raise RuntimeError(j["error"])
            return j
        time.sleep(poll)

def generic_done_wait(op_url, poll=5):
    while True:
        j = get(op_url)
        if j.get("done"):
            if "error" in j: raise RuntimeError(j["error"])
            return j
        time.sleep(poll)

# -------- plan templating --------
PLACEHOLDER = re.compile(r"\$\{steps\.([^.]+)\.result\.([^\}]+)\}")
def resolve_placeholders(obj, results):
    if isinstance(obj, dict):
        return {k: resolve_placeholders(v, results) for k, v in obj.items()}
    if isinstance(obj, list):
        return [resolve_placeholders(v, results) for v in obj]
    if isinstance(obj, str):
        def repl(m):
            step_id, path = m.group(1), m.group(2)
            cur = results.get(step_id, {}).get("result", {})
            for p in path.split("."):
                cur = cur.get(p) if isinstance(cur, dict) else None
            if cur is None: raise KeyError(f"Unresolved placeholder {m.group(0)}")
            return str(cur)
        return PLACEHOLDER.sub(repl, obj)
    return obj

# -------- up (create) handlers --------
def up_vpc_create(p):
    # idempotent: if exists, return it
    try:
        return get(f"{COMPUTE}/projects/{p['projectId']}/global/networks/{p['name']}")
    except Exception:
        pass

    u = f"{COMPUTE}/projects/{p['projectId']}/global/networks?requestId={uuid4()}"
    op = post(u, {
        "name": p["name"],
        "autoCreateSubnetworks": p.get("autoCreateSubnetworks", False),
        "routingConfig": {"routingMode": p.get("routingMode", "REGIONAL")},
        "description": p.get("description"),
        "labels": p.get("labels")
    })
    compute_wait(p["projectId"], op["name"])
    return get(f"{COMPUTE}/projects/{p['projectId']}/global/networks/{p['name']}")

def up_subnet_create(p):
    try:
        return get(f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/subnetworks/{p['name']}")
    except Exception:
        pass

    u = f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/subnetworks?requestId={uuid4()}"
    body = {
        "name": p["name"],
        "network": p["networkUri"],
        "ipCidrRange": p["ipCidrRange"],
        "privateIpGoogleAccess": p.get("privateIpGoogleAccess", True),
        "secondaryIpRanges": p.get("secondaryIpRanges", []),
        "stackType": p.get("stackType", "IPV4_ONLY"),
        "purpose": p.get("purpose", "PRIVATE"),
        "labels": p.get("labels")
    }
    op = post(u, body)
    compute_wait(p["projectId"], op["name"], region=p["region"])
    return get(f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/subnetworks/{p['name']}")

def up_router_create(p):
    try:
        return get(f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers/{p['name']}")
    except Exception:
        pass

    u = f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers?requestId={uuid4()}"
    op = post(u, {"name": p["name"], "network": p["networkUri"], "description": p.get("description")})
    compute_wait(p["projectId"], op["name"], region=p["region"])
    return get(f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers/{p['name']}")

def up_nat_create(p):
    base = f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers/{p['router']}"
    router = get(base)
    nats = [n for n in router.get("nats", []) if n.get("name") != p["name"]]
    nat = {
        "name": p["name"],
        "natIpAllocateOption": "AUTO_ONLY" if not p.get("natIps") else "MANUAL_ONLY",
        "sourceSubnetworkIpRangesToNat": p["sourceSubnetworkIpRangesToNat"],
        "enableEndpointIndependentMapping": p.get("enableEndpointIndependentMapping", True),
    }
    if p.get("natIps"): nat["natIps"] = p["natIps"]
    if p.get("subnetworks"): nat["subnetworks"] = p["subnetworks"]
    if p.get("logConfig"): nat["logConfig"] = p["logConfig"]
    nats.append(nat)
    op = patch(base, {"nats": nats})
    compute_wait(p["projectId"], op["name"], region=p["region"])
    return {"name": p["name"], "router": p["router"], "region": p["region"]}

# at top (already present): import time

def up_gke_create(req):
    p = req["params"]

    # If it already exists, just return it
    try:
        return get(f"{CONTAINER}/projects/{p['projectId']}/locations/{p['location']}/clusters/{p['name']}")
    except Exception:
        pass

    base = f"{CONTAINER}/projects/{p['projectId']}/locations/{p['location']}/clusters"
    cluster = {"name": p["name"], "network": p["networkUri"], "subnetwork": p["subnetworkUri"]}

    if req["op"] == "createClusterAutopilot":
        cluster["autopilot"] = {"enabled": True}
    else:
        # ipAllocationPolicy (map IPV4_ONLY -> IPV4)
        ipap = dict(p["ipAllocationPolicy"])
        if ipap.get("stackType") == "IPV4_ONLY":
            ipap["stackType"] = "IPV4"
        cluster["ipAllocationPolicy"] = ipap

        # ensure at least 1 node at creation
        cluster.setdefault("initialNodeCount", 1)

        # private cluster
        pc = p.get("privateCluster")
        if pc:
            cfg = {
                "enablePrivateNodes": pc.get("enablePrivateNodes", False),
                "enablePrivateEndpoint": pc.get("enablePrivateEndpoint", False),
            }
            if pc.get("masterIpv4CidrBlock"):
                cfg["masterIpv4CidrBlock"] = pc["masterIpv4CidrBlock"]
            cluster["privateClusterConfig"] = cfg
            if pc.get("masterAuthorizedNetworks"):
                cluster["masterAuthorizedNetworksConfig"] = {
                    "enabled": True,
                    "cidrBlocks": [
                        {"cidrBlock": x["cidr"], "displayName": x.get("displayName", "")}
                        for x in pc["masterAuthorizedNetworks"]
                    ],
                }

    if p.get("releaseChannel"):
        cluster["releaseChannel"] = {"channel": p["releaseChannel"]}
    if p.get("workloadIdentity"):
        cluster["workloadIdentityConfig"] = {"workloadPool": p["workloadIdentity"]["workloadPool"]}
    if p.get("shieldedNodes", True):
        cluster["shieldedNodes"] = {"enabled": True}
    if p.get("logging", {}).get("components"):
        cluster["loggingConfig"] = {"componentConfig": {"enableComponents": p["logging"]["components"]}}
    if p.get("monitoring", {}).get("managedPrometheus"):
        cluster.setdefault("monitoringConfig", {})
        cluster["monitoringConfig"]["managedPrometheusConfig"] = {"enabled": True}
    if p.get("networkDataplane"):
        cluster["networkConfig"] = {"datapathProvider": p["networkDataplane"]}

    try:
        op = post(base, {"cluster": cluster})
    except RuntimeError as e:
        msg = str(e)
        # Treat 409 as success (cluster already created)
        if "ALREADY_EXISTS" in msg or " 409 " in msg or "Already exists" in msg:
            time.sleep(5)
            return get(f"{CONTAINER}/projects/{p['projectId']}/locations/{p['location']}/clusters/{p['name']}")
        raise

    # Normalize op name to fully-qualified path
    op_name = op.get("name") or op["operation"]["name"]
    if not op_name.startswith("projects/"):
        op_name = f"projects/{p['projectId']}/locations/{p['location']}/operations/{op_name}"
    container_wait(op_name)

    return get(f"{CONTAINER}/projects/{p['projectId']}/locations/{p['location']}/clusters/{p['name']}")

def up_vpcaccess_ensure(p):
    name_full = f"projects/{p['projectId']}/locations/{p['region']}/connectors/{p['name']}"
    get_url = f"{VPCACCESS}/{name_full}"

    # Idempotent: if it exists, return it
    r = SESSION.get(get_url, headers=hdrs(), timeout=(10, 60))
    if r.status_code == 200:
        j = r.json()
        j["uri"] = j.get("name")
        return j

    # POST requires ?connectorId=<id>
    create_url = (
        f"{VPCACCESS}/projects/{p['projectId']}/locations/{p['region']}/connectors"
        f"?connectorId={p['name']}"
    )
    body = {"network": p["network"]}  # VPC name
    if p.get("subnet"):        body["subnet"] = p["subnet"]             # {"name": "<subnet-name>"}
    if p.get("ipCidrRange"):   body["ipCidrRange"] = p["ipCidrRange"]   # "10.100.0.0/28"
    if p.get("minThroughput"): body["minThroughput"] = p["minThroughput"]
    if p.get("maxThroughput"): body["maxThroughput"] = p["maxThroughput"]

    try:
        op = post(create_url, body)
    except RuntimeError as e:
        msg = str(e)
        # concurrent create or eventual GET race: treat as success
        if "ALREADY_EXISTS" in msg or " 409 " in msg or "Already exists" in msg:
            time.sleep(3)
            j = get(get_url)
            j["uri"] = j.get("name")
            return j
        raise

    # LRO may complete before GET is immediately stable; poll a few times
    generic_done_wait(f"{VPCACCESS}/{op['name']}")
    for attempt in range(8):
        try:
            j = get(get_url)
            j["uri"] = j.get("name")
            return j
        except Exception:
            time.sleep(0.7 * (attempt + 1))
            continue
    # final attempt—surface real error if any
    j = get(get_url)
    j["uri"] = j.get("name")
    return j


def up_cloudrun_deploy(p):
    parent   = f"projects/{p['projectId']}/locations/{p['region']}"
    svc_id   = p["name"]
    svc_name = f"{parent}/services/{svc_id}"
    get_url  = f"{RUN}/{svc_name}"
    post_url = f"{RUN}/{parent}/services?serviceId={svc_id}"

    # Idempotent: if it already exists, return it
    r = SESSION.get(get_url, headers=hdrs(), timeout=(10, 60))
    if r.status_code == 200:
        j = r.json()
        return {"name": j["name"], "uri": j.get("uri", "")}

    # Cloud Run v2: Service resource at top-level, NO 'name' on create body.
    body = {
        "ingress": p.get("ingress", "INGRESS_TRAFFIC_ALL"),
        "template": {
            "containers": [{
                "image": p["image"],
                "ports": [{"containerPort": p.get("containerPort", 8080)}],
            }],
            "maxInstanceRequestConcurrency": p.get("concurrency", 80),
            "scaling": {
                "minInstanceCount": p.get("scaling", {}).get("min", 0),
                "maxInstanceCount": p.get("scaling", {}).get("max", 10),
            },
            "executionEnvironment": p.get("executionEnvironment", "EXECUTION_ENVIRONMENT_GEN2"),
        },
        "traffic": [{"type": "TRAFFIC_TARGET_ALLOCATION_TYPE_LATEST", "percent": 100}],
    }
    if p.get("serviceAccount"):
        body["template"]["serviceAccount"] = p["serviceAccount"]
    if p.get("vpcAccess"):
        body["template"]["vpcAccess"] = {
            "connector": p["vpcAccess"]["connector"],
            "egress": p["vpcAccess"].get("egress", "PRIVATE_RANGES_ONLY"),
        }

    # Create (treat 409 as success)
    try:
        op = post(post_url, body)
    except RuntimeError as e:
        msg = str(e)
        if "ALREADY_EXISTS" in msg or " 409 " in msg or "Already exists" in msg:
            time.sleep(2)
            j = get(get_url)
            return {"name": j["name"], "uri": j.get("uri", "")}
        raise

    # LRO wait (v2 returns operations)
    if isinstance(op, dict) and "name" in op:
        generic_done_wait(f"{RUN}/{op['name']}")

    # Poll GET to ride out eventual consistency
    for attempt in range(10):
        try:
            j = get(get_url)
            return {"name": j["name"], "uri": j.get("uri", "")}
        except RuntimeError as e:
            if " 404 " in str(e) and attempt < 9:
                time.sleep(0.8 * (attempt + 1))
                continue
            raise

# -------- down (destroy) helpers --------
def down_cloudrun_delete(p):
    svc = f"{RUN}/projects/{p['projectId']}/locations/{p['region']}/services/{p['name']}"
    r = requests.delete(svc, headers=hdrs())
    if r.status_code in (200,202):
        op = r.json()
        generic_done_wait(f"{RUN}/{op['name']}")
    elif r.status_code == 404:
        return
    else:
        _req("DELETE", svc)  # raise

def down_vpcaccess_delete(p):
    url = f"{VPCACCESS}/projects/{p['projectId']}/locations/{p['region']}/connectors/{p['name']}"
    r = requests.delete(url, headers=hdrs())
    if r.status_code in (200,202):
        op = r.json()
        generic_done_wait(f"{VPCACCESS}/{op['name']}")
    elif r.status_code == 404:
        return
    else:
        _req("DELETE", url)

def _region_zones(project, region):
    j = get(f"{COMPUTE}/projects/{project}/regions/{region}")
    return [z.split("/")[-1] for z in j.get("zones", [])]

def _delete_igm(project, zone, name):
    url = f"{COMPUTE}/projects/{project}/zones/{zone}/instanceGroupManagers/{name}"
    r = SESSION.delete(url, headers=hdrs(), timeout=(10, 120))
    if r.status_code == 404:
        return
    op = r.json()
    compute_wait(project, op.get("name", ""), zone=zone)

def _delete_ig(project, zone, name):
    url = f"{COMPUTE}/projects/{project}/zones/{zone}/instanceGroups/{name}"
    r = SESSION.delete(url, headers=hdrs(), timeout=(10, 120))
    if r.status_code == 404:
        return
    op = r.json()
    compute_wait(project, op.get("name", ""), zone=zone)

def down_gke_delete(p):
    # Issue cluster DELETE
    url = f"{CONTAINER}/projects/{p['projectId']}/locations/{p['location']}/clusters/{p['name']}"
    r = SESSION.delete(url, headers=hdrs(), timeout=(10, 120))
    if r.status_code in (200, 201, 202):
        op = r.json()
        op_name = op.get("name") or op.get("operation", {}).get("name", "")
        if op_name and not op_name.startswith("projects/"):
            op_name = f"projects/{p['projectId']}/locations/{p['location']}/operations/{op_name}"
        if op_name:
            container_wait(op_name)
    elif r.status_code != 404:
        # surface other errors
        _req("DELETE", url)  # raises

    # Poll until the cluster is 404 (up to ~2 minutes)
    gone = False
    for attempt in range(12):
        try:
            _ = get(url)  # still exists
            time.sleep(5 + attempt)
        except RuntimeError as e:
            if " 404 " in str(e):
                gone = True
                break
            raise
    # Best-effort cleanup of lingering instance groups/IGMs created by GKE
    # GKE names look like gke-<cluster>-<pool>-<hash>-grp
    prefix = f"gke-{p['name']}-"
    for zone in _region_zones(p["projectId"], p["location"]):
        # list IGMs
        try:
            igms = get(f"{COMPUTE}/projects/{p['projectId']}/zones/{zone}/instanceGroupManagers").get("items", [])
            for igm in igms:
                nm = igm.get("name", "")
                if nm.startswith(prefix):
                    _delete_igm(p["projectId"], zone, nm)
        except Exception:
            pass
        # list unmanaged IGs (sometimes remain)
        try:
            igs = get(f"{COMPUTE}/projects/{p['ProjectId']}/zones/{zone}/instanceGroups").get("items", [])
        except Exception:
            igs = []
        for ig in igs:
            nm = ig.get("name", "")
            if nm.startswith(prefix):
                try:
                    _delete_ig(p["projectId"], zone, nm)
                except Exception:
                    pass

def down_nat_delete(p):
    base = f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers/{p['router']}"
    try:
        router = get(base)
    except Exception:
        return
    nats = [n for n in router.get("nats", []) if n.get("name") != p["name"]]
    if len(nats) == len(router.get("nats", [])):  # not present
        return
    op = patch(base, {"nats": nats})
    compute_wait(p["projectId"], op["name"], region=p["region"])

def down_router_delete(p):
    url = f"{COMPUTE}/projects/{p['projectId']}/regions/{p['region']}/routers/{p['name']}"
    r = requests.delete(url, headers=hdrs())
    if r.status_code == 404:
        return
    op = r.json()
    compute_wait(p["projectId"], op["name"], region=p["region"])

def down_subnet_delete(p):
    pj = p.get("projectId")
    rg = p.get("region")
    nm = p.get("name")
    if not (pj and rg and nm):
        return  # nothing to do if params are incomplete
    url = f"{COMPUTE}/projects/{pj}/regions/{rg}/subnetworks/{nm}"
    r = SESSION.delete(url, headers=hdrs(), timeout=(10, 120))
    if r.status_code == 404:
        return
    op = r.json()
    compute_wait(pj, op.get("name", ""), region=rg)

def list_firewalls(project):
    out = []
    page = None
    while True:
        url = f"{COMPUTE}/projects/{project}/global/firewalls"
        if page: url += f"?pageToken={page}"
        j = get(url)
        out.extend(j.get("items", []))
        page = j.get("nextPageToken")
        if not page: break
    return out

def down_vpc_delete_with_firewalls(p, mode="gke", cluster_name=None, stack=None):
    # optional firewall cleanup
    net = f"{COMPUTE}/projects/{p['projectId']}/global/networks/{p['name']}"
    if mode != "none":
        fws = list_firewalls(p["projectId"])
        to_del = []
        for fw in fws:
            if fw.get("network") != net: continue
            name = fw.get("name","")
            if mode == "all":
                to_del.append(name)
            elif mode == "gke":
                if name.startswith("gke-") or (cluster_name and cluster_name in name) or (stack and stack in name):
                    to_del.append(name)
        for name in to_del:
            url = f"{COMPUTE}/projects/{p['projectId']}/global/firewalls/{name}"
            try:
                r = requests.delete(url, headers=hdrs())
                if r.status_code == 404: continue
                op = r.json()
                compute_wait(p["projectId"], op["name"])
            except Exception as e:
                print(f"[warn] firewall {name}: {e}")

    # delete the VPC
    url = f"{COMPUTE}/projects/{p['projectId']}/global/networks/{p['name']}"
    r = requests.delete(url, headers=hdrs())
    if r.status_code == 404:
        return
    op = r.json()
    compute_wait(p["projectId"], op["name"])

# -------- dispatchers --------
def run_step_up(step, results):
    req = json.loads(json.dumps(step["request"]))  # deep copy
    req["params"] = resolve_placeholders(req["params"], results)
    op = req["op"]; p = req["params"]; plugin = step["plugin"]

    if plugin == "gcp.vpc"       and op == "createNetwork":          return up_vpc_create(p)
    if plugin == "gcp.subnet"    and op == "createSubnetwork":       return up_subnet_create(p)
    if plugin == "gcp.cloudnat"  and op == "createRouter":           return up_router_create(p)
    if plugin == "gcp.cloudnat"  and op == "createNat":              return up_nat_create(p)
    if plugin == "gcp.gke"       and op in ("createClusterStandard","createClusterAutopilot"):
        return up_gke_create(req)
    if plugin == "gcp.vpcaccess" and op == "ensureConnector":        return up_vpcaccess_ensure(p)
    if plugin == "gcp.cloudrun"  and op == "deployService":          return up_cloudrun_deploy(p)

    raise NotImplementedError(f"Unsupported up op: {plugin}:{op}")

def run_step_down(step, fw_mode, cluster_name, stack):
    """Use the original step params to know names; delete in reverse order."""
    req = step["request"]; op = req["op"]; p = req["params"]; plugin = step["plugin"]

    if plugin == "gcp.cloudrun"  and op == "deployService":
        return down_cloudrun_delete(p)
    if plugin == "gcp.vpcaccess" and op == "ensureConnector":
        return down_vpcaccess_delete(p)
    if plugin == "gcp.gke"       and op in ("createClusterStandard","createClusterAutopilot"):
        return down_gke_delete({"projectId": p["projectId"], "location": p["location"], "name": p["name"]})
    if plugin == "gcp.cloudnat"  and op == "createNat":
        return down_nat_delete(p)
    if plugin == "gcp.cloudnat"  and op == "createRouter":
        return down_router_delete(p)
    if plugin == "gcp.subnet"    and op == "createSubnetwork":
        return down_subnet_delete(p)
    if plugin == "gcp.vpc"       and op == "createNetwork":
        return down_vpc_delete_with_firewalls(
            p, mode=fw_mode, cluster_name=cluster_name, stack=stack
        )
    # Ignore other ops quietly
    return None

# -------- main --------
def main():
    ap = argparse.ArgumentParser(description="Execute or destroy a stack compose plan.")
    ap.add_argument("--destroy", action="store_true", help="Tear down resources instead of creating them")
    ap.add_argument("--fw-cleanup", choices=["none","gke","all"], default="gke",
                    help="On VPC delete: which firewall rules on the network to remove first")
    ap.add_argument("plan", help="stack-<name>.json produced by make_stack_compose.py")
    args = ap.parse_args()

    plan = json.load(open(args.plan))
    steps = plan["steps"]
    stack = plan.get("metadata",{}).get("name","")

    if not args.destroy:
        # apply (up)
        print(f"Applying plan: {stack}")
        done, results = set(), {}
        def ready(s): return all(d in done for d in s.get("dependsOn", []))
        while len(done) < len(steps):
            progress = False
            for s in steps:
                if s["id"] in done: continue
                if not ready(s): continue
                print(f"→ {s['id']} ({s['plugin']}:{s['request']['op']})")
                res = run_step_up(s, results)
                results[s["id"]] = {"result": res}
                done.add(s["id"]); progress = True
            if not progress: raise RuntimeError("Deadlock in dependsOn; check your plan.")

        outs = {k: resolve_placeholders(v, results) if isinstance(v, str) else v
                for k, v in plan.get("outputs", {}).items()}
        print("\nOutputs:")
        print(json.dumps(outs, indent=2))
        return

    # destroy (down) – strict reverse order
    print(f"Destroying plan: {stack}")
    # We’ll pass cluster name to firewall cleanup heuristic
    cluster_name = None
    for s in steps:
        if s["plugin"] == "gcp.gke":
            cluster_name = s["request"]["params"].get("name")
            break

    for s in reversed(steps):
        print(f"↘ delete {s['id']} ({s['plugin']}:{s['request']['op']})")
        try:
            run_step_down(s, args.fw_cleanup, cluster_name, stack)
        except Exception as e:
            print(f"[warn] {s['id']}: {e}")

    print("Destroy completed.")

if __name__ == "__main__":
    main()
