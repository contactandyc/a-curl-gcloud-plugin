#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025–2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
#
# Maintainer: Andy Curtis <contactandyc@gmail.com>

"""
Generate a stack compose plan that provisions:
VPC → Subnet (with secondary ranges) → Cloud Router/NAT → GKE (Standard) → VPC Access → Cloud Run.

The plan uses a simple DAG format your orchestrator can execute:
  {
    "apiVersion": "stack.compose/v1",
    "kind": "Plan",
    "metadata": {"name": "<stack>"},
    "vars": {...},
    "steps": [
      {
        "id": "vpc_create",
        "plugin": "gcp.vpc",
        "request": {"op": "createNetwork", "params": {...}},
        "dependsOn": []
      },
      ...
    ],
    "outputs": { ... }
  }

Later steps reference earlier results with ${steps.<id>.result.<field>} placeholders.
Adjust the placeholders to your runtime’s templating syntax if needed.
"""
import argparse, json, sys, pathlib

def build_plan(
    project_id: str,
    region: str,
    stack: str,
    subnet_cidr: str,
    pods_cidr: str,
    services_cidr: str,
    gke_release_channel: str,
    gke_private: bool,
    gke_master_cidr: str | None,
    cloud_run_image: str,
    cloud_run_ingress: str,
    cloud_run_egress: str,
    cloud_run_concurrency: int,
    min_instances: int,
    max_instances: int,
    with_vpc_access: bool,
    autopilot: bool,
    conn_subnet_cidr: str,
    conn_subnet_name: str | None,
):
    # ---- Canonical names used across the stack
    vpc_name         = f"{stack}-vpc"
    subnet_name      = f"{stack}-subnet"
    pods_range_name  = f"{stack}-pods"
    svcs_range_name  = f"{stack}-services"
    router_name      = f"{stack}-router"
    nat_name         = f"{stack}-nat"
    gke_cluster_name = f"{stack}-gke"
    vpcacc_name      = f"{stack}-conn"
    run_service_name = f"{stack}-api"
    svpc_subnet_name  = conn_subnet_name or f"{stack}-svpc"

    # ---- Common expressions to wire outputs between steps
    vpc_uri_expr        = "${steps.vpc_create.result.selfLink}"
    subnet_uri_expr     = "${steps.subnet_create.result.selfLink}"
    router_uri_expr     = "${steps.router_create.result.selfLink}"   # informational output
    nat_name_expr       = "${steps.nat_create.result.name}"
    gke_endpoint_expr   = "${steps.gke_create.result.endpoint}"
    conn_uri_expr       = "${steps.vpcaccess_ensure.result.uri}"
    run_url_expr        = "${steps.cloudrun_deploy.result.uri}"

    # ---- Steps
    steps = []

    # 1) VPC
    steps.append({
        "id": "vpc_create",
        "plugin": "gcp.vpc",
        "request": {
            "op": "createNetwork",
            "params": {
                "projectId": project_id,
                "name": vpc_name,
                "autoCreateSubnetworks": False,
                "routingMode": "REGIONAL",
                "labels": {"stack": stack}
            }
        },
        "dependsOn": []
    })

    # 2) Subnet (with two secondary ranges for GKE IP aliases)
    steps.append({
        "id": "subnet_create",
        "plugin": "gcp.subnet",
        "request": {
            "op": "createSubnetwork",
            "params": {
                "projectId": project_id,
                "region": region,
                "name": subnet_name,
                "networkUri": vpc_uri_expr,
                "ipCidrRange": subnet_cidr,
                "privateIpGoogleAccess": True,
                "secondaryIpRanges": [
                    {"rangeName": pods_range_name, "ipCidrRange": pods_cidr},
                    {"rangeName": svcs_range_name, "ipCidrRange": services_cidr}
                ],
                "stackType": "IPV4_ONLY",
                "labels": {"stack": stack}
            }
        },
        "dependsOn": ["vpc_create"]
    })

    # 2b) Dedicated /28 subnet for Serverless VPC Access connector
    if with_vpc_access:
        steps.append({
            "id": "subnet_svpc_create",
            "plugin": "gcp.subnet",
            "request": {
                "op": "createSubnetwork",
                "params": {
                    "projectId": project_id,
                    "region": region,
                    "name": svpc_subnet_name,
                    "networkUri": vpc_uri_expr,
                    "ipCidrRange": conn_subnet_cidr,
                    "privateIpGoogleAccess": True,
                    "labels": {"stack": stack, "role": "svpc"}
                }
            },
            "dependsOn": ["vpc_create"]
        })

    # 3) Cloud Router
    steps.append({
        "id": "router_create",
        "plugin": "gcp.cloudnat",
        "request": {
            "op": "createRouter",
            "params": {
                "projectId": project_id,
                "region": region,
                "name": router_name,
                "networkUri": vpc_uri_expr,
                "description": f"{stack} router"
            }
        },
        "dependsOn": ["vpc_create"]
    })

    # 4) Cloud NAT (cover all subnets in region)
    steps.append({
        "id": "nat_create",
        "plugin": "gcp.cloudnat",
        "request": {
            "op": "createNat",
            "params": {
                "projectId": project_id,
                "region": region,
                "router": router_name,
                "name": nat_name,
                "sourceSubnetworkIpRangesToNat": "ALL_SUBNETWORKS_ALL_IP_RANGES",
                "enableEndpointIndependentMapping": True,
                "logConfig": {"enable": False, "filter": "ERRORS_ONLY"}
            }
        },
        "dependsOn": ["router_create", "subnet_create"]
    })

    # 5) GKE Cluster (Standard to showcase secondary range wiring)
    if autopilot:
        # Autopilot (simpler params, no explicit secondary range wiring)
        gke_request = {
            "op": "createClusterAutopilot",
            "params": {
                "projectId": project_id,
                "location": region,  # regional Autopilot
                "name": gke_cluster_name,
                "networkUri": vpc_uri_expr,
                "subnetworkUri": subnet_uri_expr,
                "releaseChannel": gke_release_channel,
                "workloadIdentity": {"workloadPool": f"{project_id}.svc.id.goog"},
                "labels": {"stack": stack}
            }
        }
    else:
        # Standard with IP aliases referencing the secondary ranges created above
        gke_request = {
            "op": "createClusterStandard",
            "params": {
                "projectId": project_id,
                "location": region,  # can be region or zone; using region for an HA control plane
                "name": gke_cluster_name,
                "networkUri": vpc_uri_expr,
                "subnetworkUri": subnet_uri_expr,
                "releaseChannel": gke_release_channel,
                "ipAllocationPolicy": {
                    "useIpAliases": True,
                    "clusterSecondaryRangeName": pods_range_name,
                    "servicesSecondaryRangeName": svcs_range_name,
                    "stackType": "IPV4_ONLY"
                },
                "privateCluster": (
                    {"enablePrivateNodes": True,
                     "enablePrivateEndpoint": False,
                     **({"masterIpv4CidrBlock": gke_master_cidr} if gke_master_cidr else {}),
                     "masterAuthorizedNetworks": []}
                    if gke_private else {}
                ),
                "workloadIdentity": {"workloadPool": f"{project_id}.svc.id.goog"},
                "shieldedNodes": True,
                "logging": {"components": ["SYSTEM_COMPONENTS", "WORKLOADS"]},
                "monitoring": {"managedPrometheus": True},
                "networkDataplane": "DATAPATH_PROVIDER_UNSPECIFIED",
                "labels": {"stack": stack}
            }
        }

    steps.append({
        "id": "gke_create",
        "plugin": "gcp.gke",
        "request": gke_request,
        "dependsOn": ["subnet_create", "nat_create"]
    })

    # 6) VPC Access connector (optional but enabled by default for Cloud Run → VPC)
    if with_vpc_access:
        steps.append({
            "id": "vpcaccess_ensure",
            "plugin": "gcp.vpcaccess",
            "request": {
                "op": "ensureConnector",
                "params": {
                    "projectId": project_id,
                    "region": region,
                    "name": vpcacc_name,
                    "network": vpc_name,
                    # IMPORTANT: bind to the dedicated /28 subnet
                    "subnet": { "name": svpc_subnet_name },
                    "minThroughput": 200,
                    "maxThroughput": 300
                }
            },
            # make it wait for the connector subnet
            "dependsOn": ["subnet_svpc_create"]
        })

    # 7) Cloud Run (wired to VPC connector if created)
    run_params = {
        "projectId": project_id,
        "region": region,
        "name": run_service_name,
        "image": cloud_run_image,
        "containerPort": 8080,
        "concurrency": cloud_run_concurrency,
        "scaling": {"min": min_instances, "max": max_instances},
        "ingress": cloud_run_ingress,
        "labels": {"stack": stack}
    }
    if with_vpc_access:
        run_params["vpcAccess"] = {
            "connector": conn_uri_expr,
            "egress": cloud_run_egress
        }

    steps.append({
        "id": "cloudrun_deploy",
        "plugin": "gcp.cloudrun",
        "request": {"op": "deployService", "params": run_params},
        "dependsOn": (["vpcaccess_ensure"] if with_vpc_access else ["vpc_create"])
    })

    plan = {
        "apiVersion": "stack.compose/v1",
        "kind": "Plan",
        "metadata": {"name": stack},
        "vars": {
            "projectId": project_id,
            "region": region,
            "stack": stack,
            "subnetCidr": subnet_cidr,
            "podsCidr": pods_cidr,
            "servicesCidr": services_cidr,
            "connSubnetCidr": conn_subnet_cidr,
            "connSubnetName": svpc_subnet_name
        },
        "steps": steps,
        "outputs": {
            "vpcUri": vpc_uri_expr,
            "subnetUri": subnet_uri_expr,
            "routerUri": router_uri_expr,
            "natName": nat_name_expr,
            "gkeCluster": gke_cluster_name,
            "gkeEndpoint": gke_endpoint_expr,
            "vpcAccessConnectorUri": (conn_uri_expr if with_vpc_access else None),
            "cloudRunUrl": run_url_expr
        }
    }

    # Drop null outputs to keep plan clean if vpc access is disabled
    plan["outputs"] = {k: v for k, v in plan["outputs"].items() if v is not None}
    return plan


def main():
    p = argparse.ArgumentParser(description="Create a stack compose plan for VPC → Subnet → NAT → GKE → (VPC Access) → Cloud Run.")
    p.add_argument("--project", required=True, help="GCP project ID")
    p.add_argument("--region", required=True, help="GCP region (e.g., us-central1)")
    p.add_argument("--stack",  required=True, help="Short name used to prefix resources (e.g., demo)")
    p.add_argument("--subnet-cidr",    default="10.10.0.0/20", help="Primary subnet CIDR")
    p.add_argument("--pods-cidr",      default="10.20.0.0/16", help="Secondary CIDR for GKE Pods")
    p.add_argument("--services-cidr",  default="10.21.0.0/20", help="Secondary CIDR for GKE Services")
    p.add_argument("--gke-release",    default="REGULAR", choices=["RAPID","REGULAR","STABLE"], help="GKE release channel")
    p.add_argument("--gke-private",    action="store_true", help="Create a private GKE cluster")
    p.add_argument("--gke-master-cidr", default=None, help="Control plane CIDR for private cluster (e.g., 172.16.0.0/28)")
    p.add_argument("--autopilot",      action="store_true", help="Use GKE Autopilot instead of Standard")
    p.add_argument("--run-image",      default="us-docker.pkg.dev/cloudrun/container/hello:latest", help="Container image for Cloud Run")
    p.add_argument("--run-ingress",    default="INGRESS_TRAFFIC_ALL",
                   choices=["INGRESS_TRAFFIC_ALL","INGRESS_TRAFFIC_INTERNAL_ONLY","INGRESS_TRAFFIC_INTERNAL_LOAD_BALANCER"],
                   help="Cloud Run ingress policy")
    p.add_argument("--run-egress",     default="PRIVATE_RANGES_ONLY",
                   choices=["PRIVATE_RANGES_ONLY","ALL_TRAFFIC"],
                   help="Cloud Run VPC egress policy (used when VPC Access is enabled)")
    p.add_argument("--run-concurrency", type=int, default=80, help="Cloud Run container concurrency")
    p.add_argument("--min-instances",   type=int, default=0,  help="Cloud Run min instances")
    p.add_argument("--max-instances",   type=int, default=10, help="Cloud Run max instances")
    p.add_argument("--no-vpc-access",   action="store_true",  help="Do NOT create a VPC Access connector; Cloud Run stays public only")
    p.add_argument("--conn-subnet-cidr", default="10.100.0.0/28",
                   help="CIDR for the dedicated Serverless VPC Access subnet (/28 recommended)")
    p.add_argument("--conn-subnet-name", default=None,
                   help="Override name for the connector subnet (default: <stack>-svpc)")

    p.add_argument("-o", "--out",       default=None, help="Output file (defaults to stack-<stack>.json)")
    args = p.parse_args()

    with_vpc_access = not args.no_vpc_access

    plan = build_plan(
        project_id=args.project,
        region=args.region,
        stack=args.stack,
        subnet_cidr=args.subnet_cidr,
        pods_cidr=args.pods_cidr,
        services_cidr=args.services_cidr,
        gke_release_channel=args.gke_release,
        gke_private=args.gke_private,
        gke_master_cidr=args.gke_master_cidr,
        cloud_run_image=args.run_image,
        cloud_run_ingress=args.run_ingress,
        cloud_run_egress=args.run_egress,
        cloud_run_concurrency=args.run_concurrency,
        min_instances=args.min_instances,
        max_instances=args.max_instances,
        with_vpc_access=with_vpc_access,
        autopilot=args.autopilot,
        conn_subnet_cidr=args.conn_subnet_cidr,
        conn_subnet_name=args.conn_subnet_name,
    )

    out_path = pathlib.Path(args.out or f"stack-{args.stack}.json")
    out_path.write_text(json.dumps(plan, indent=2))
    print(f"Wrote: {out_path}")

if __name__ == "__main__":
    main()
