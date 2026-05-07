Awesome — here are **reference JSON Schemas (Draft 2020‑12)** for each plugin we’ve defined so far.
**Shape:** every plugin uses a uniform **RPC envelope**:

* **Request:**

  ```json
  { "op": "<operationName>", "params": { ... } }
  ```
* **Response:**

  ```json
  { "op": "<operationName>", "result": { ... } }
  ```

Each plugin schema validates the allowed `op` values and the structure of the corresponding `params` / `result`. I’ve kept resource shapes focused on fields you’ll commonly need; Google’s APIs may return additional fields, which your runtime can pass through if desired.

---

## 0) Common primitives (import into all plugins)

```json
{
  "$id": "https://schemas.example.com/gcp/common.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "GCP Common Primitives",
  "type": "object",
  "$defs": {
    "ProjectId": {
      "type": "string",
      "pattern": "^[a-z][a-z0-9-]{4,28}[a-z0-9]$"
    },
    "Region": {
      "type": "string",
      "pattern": "^[a-z]+-[a-z0-9]+[0-9]$",
      "examples": ["us-central1", "europe-west1"]
    },
    "Zone": {
      "type": "string",
      "pattern": "^[a-z]+-[a-z0-9]+[0-9]-[a-z]$",
      "examples": ["us-central1-a"]
    },
    "Location": {
      "type": "string",
      "pattern": "^[a-z]+-[a-z0-9]+[0-9](-[a-z])?$",
      "description": "Region or zone."
    },
    "Labels": {
      "type": "object",
      "description": "GCP labels: 1-63 chars, lowercase letters, numbers, underscores, and dashes.",
      "propertyNames": { "pattern": "^[a-z0-9_-]{1,63}$" },
      "additionalProperties": {
        "type": "string",
        "maxLength": 63
      }
    },
    "IPv4Cidr": {
      "type": "string",
      "pattern": "^(?:\\d{1,3}\\.){3}\\d{1,3}\\/(?:[0-9]|[12]\\d|3[0-2])$",
      "examples": ["10.0.0.0/16", "192.168.100.0/24"]
    },
    "ResourceUri": { "type": "string", "minLength": 1 },
    "BoolDefaultFalse": { "type": "boolean", "default": false },
    "Timestamp": { "type": "string", "format": "date-time" },

    "ComputeName63": {
      "type": "string",
      "pattern": "^[a-z]([-a-z0-9]{0,61}[a-z0-9])?$"
    },
    "RunServiceName": {
      "type": "string",
      "pattern": "^[a-z]([-a-z0-9]{0,61}[a-z0-9])?$"
    },
    "GkeClusterName": { "$ref": "#/$defs/ComputeName63" },
    "GkeNodePoolName": { "$ref": "#/$defs/ComputeName63" },

    "SecondaryRange": {
      "type": "object",
      "required": ["rangeName", "ipCidrRange"],
      "properties": {
        "rangeName": { "$ref": "#/$defs/ComputeName63" },
        "ipCidrRange": { "$ref": "#/$defs/IPv4Cidr" }
      },
      "additionalProperties": false
    },

    "LroNote": {
      "type": "string",
      "const": "This plugin polls the underlying Google LRO until DONE and returns the final resource or an error."
    }
  }
}
```

> In the plugin schemas below, import with `"$ref": "https://schemas.example.com/gcp/common.json#/$defs/<Name>"` (or inline the `$defs` if you prefer single files).

---

## 1) `gcp.vpc` — **VPC Networks**

### Request schema

```json
{
  "$id": "https://schemas.example.com/gcp/vpc.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.vpc Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": ["createNetwork", "getNetwork", "listNetworks", "deleteNetwork"]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "createNetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "autoCreateSubnetworks": { "type": "boolean", "default": false },
              "routingMode": { "type": "string", "enum": ["REGIONAL", "GLOBAL"], "default": "REGIONAL" },
              "description": { "type": "string" },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getNetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "listNetworks" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteNetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response schema

```json
{
  "$id": "https://schemas.example.com/gcp/vpc.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.vpc Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Network" },
        {
          "type": "object",
          "required": ["items"],
          "properties": { "items": { "type": "array", "items": { "$ref": "#/$defs/Network" } } },
          "additionalProperties": false
        },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Network": {
      "type": "object",
      "required": ["projectId", "name", "selfLink", "autoCreateSubnetworks", "routingMode"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "selfLink": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "autoCreateSubnetworks": { "type": "boolean" },
        "routingMode": { "type": "string", "enum": ["REGIONAL", "GLOBAL"] },
        "description": { "type": "string" },
        "creationTimestamp": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Timestamp" },
        "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 2) `gcp.subnet` — **Subnets & secondary ranges**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/subnet.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.subnet Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": ["createSubnetwork", "getSubnetwork", "listSubnetworks", "deleteSubnetwork"]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "createSubnetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "networkUri", "ipCidrRange"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "ipCidrRange": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/IPv4Cidr" },
              "privateIpGoogleAccess": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/BoolDefaultFalse" },
              "secondaryIpRanges": {
                "type": "array",
                "items": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/SecondaryRange" },
                "default": []
              },
              "purpose": {
                "type": "string",
                "enum": ["PRIVATE", "REGIONAL_MANAGED_PROXY", "GLOBAL_MANAGED_PROXY"],
                "default": "PRIVATE",
                "description": "Common purposes; omit unless creating special-purpose subnets."
              },
              "stackType": { "type": "string", "enum": ["IPV4_ONLY", "IPV4_IPV6"], "default": "IPV4_ONLY" },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getSubnetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "listSubnetworks" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteSubnetwork" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/subnet.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.subnet Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Subnetwork" },
        {
          "type": "object",
          "required": ["items"],
          "properties": { "items": { "type": "array", "items": { "$ref": "#/$defs/Subnetwork" } } },
          "additionalProperties": false
        },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Subnetwork": {
      "type": "object",
      "required": ["projectId", "region", "name", "networkUri", "ipCidrRange", "selfLink"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "ipCidrRange": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/IPv4Cidr" },
        "privateIpGoogleAccess": { "type": "boolean" },
        "secondaryIpRanges": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["rangeName", "ipCidrRange"],
            "properties": {
              "rangeName": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "ipCidrRange": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/IPv4Cidr" }
            },
            "additionalProperties": false
          }
        },
        "selfLink": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "stackType": { "type": "string", "enum": ["IPV4_ONLY", "IPV4_IPV6"] },
        "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 3) `gcp.cloudnat` — **Cloud Router & Cloud NAT**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/cloudnat.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.cloudnat Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": ["createRouter", "getRouter", "createNat", "getNat", "deleteNat"]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "createRouter" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "networkUri"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "description": { "type": "string" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getRouter" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "createNat" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "router", "name", "sourceSubnetworkIpRangesToNat"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "router": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "sourceSubnetworkIpRangesToNat": {
                "type": "string",
                "enum": ["ALL_SUBNETWORKS_ALL_IP_RANGES", "ALL_SUBNETWORKS_ALL_PRIMARY_IP_RANGES", "LIST_OF_SUBNETWORKS"]
              },
              "subnetworks": {
                "type": "array",
                "description": "Required if sourceSubnetworkIpRangesToNat=LIST_OF_SUBNETWORKS",
                "items": {
                  "type": "object",
                  "required": ["name"],
                  "properties": {
                    "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
                    "sourceIpRangesToNat": {
                      "type": "array",
                      "items": { "type": "string", "enum": ["ALL_IP_RANGES", "PRIMARY_IP_RANGE", "LIST_OF_SECONDARY_IP_RANGES"] }
                    },
                    "secondaryIpRangeNames": {
                      "type": "array",
                      "items": { "type": "string" }
                    }
                  },
                  "additionalProperties": false
                },
                "default": []
              },
              "natIps": {
                "type": "array",
                "description": "Optional static addresses (URIs). If omitted, auto-allocation.",
                "items": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" }
              },
              "enableEndpointIndependentMapping": { "type": "boolean", "default": true },
              "logConfig": {
                "type": "object",
                "properties": {
                  "enable": { "type": "boolean", "default": false },
                  "filter": { "type": "string", "enum": ["ERRORS_ONLY", "TRANSLATIONS_ONLY", "ALL"], "default": "ERRORS_ONLY" }
                },
                "additionalProperties": false
              }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getNat" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "router", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "router": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteNat" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "router", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "router": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/cloudnat.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.cloudnat Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Router" },
        { "$ref": "#/$defs/Nat" },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Router": {
      "type": "object",
      "required": ["projectId", "region", "name", "networkUri", "selfLink"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "selfLink": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "creationTimestamp": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Timestamp" }
      },
      "additionalProperties": true
    },
    "Nat": {
      "type": "object",
      "required": ["projectId", "region", "router", "name"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
        "router": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "natIps": { "type": "array", "items": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" } },
        "sourceSubnetworkIpRangesToNat": {
          "type": "string",
          "enum": ["ALL_SUBNETWORKS_ALL_IP_RANGES", "ALL_SUBNETWORKS_ALL_PRIMARY_IP_RANGES", "LIST_OF_SUBNETWORKS"]
        },
        "selfLink": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "logConfig": {
          "type": "object",
          "properties": {
            "enable": { "type": "boolean" },
            "filter": { "type": "string", "enum": ["ERRORS_ONLY", "TRANSLATIONS_ONLY", "ALL"] }
          }
        }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 4) `gcp.gke` — **GKE Standard & Autopilot, Node Pools**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/gke.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.gke Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": [
        "createClusterStandard", "createClusterAutopilot",
        "getCluster", "listClusters", "deleteCluster",
        "createNodePool", "getNodePool", "listNodePools", "deleteNodePool"
      ]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "createClusterStandard" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "name", "networkUri", "subnetworkUri", "ipAllocationPolicy"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
              "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "subnetworkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "releaseChannel": { "type": "string", "enum": ["RAPID", "REGULAR", "STABLE"], "default": "REGULAR" },
              "ipAllocationPolicy": {
                "type": "object",
                "required": ["useIpAliases", "clusterSecondaryRangeName", "servicesSecondaryRangeName"],
                "properties": {
                  "useIpAliases": { "type": "boolean", "const": true },
                  "clusterSecondaryRangeName": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
                  "servicesSecondaryRangeName": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
                  "stackType": { "type": "string", "enum": ["IPV4_ONLY", "IPV4_IPV6"], "default": "IPV4_ONLY" }
                },
                "additionalProperties": false
              },
              "privateCluster": {
                "type": "object",
                "properties": {
                  "enablePrivateNodes": { "type": "boolean", "default": false },
                  "enablePrivateEndpoint": { "type": "boolean", "default": false },
                  "masterIpv4CidrBlock": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/IPv4Cidr" },
                  "masterAuthorizedNetworks": {
                    "type": "array",
                    "items": {
                      "type": "object",
                      "required": ["cidr"],
                      "properties": {
                        "cidr": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/IPv4Cidr" },
                        "displayName": { "type": "string" }
                      },
                      "additionalProperties": false
                    }
                  }
                },
                "additionalProperties": false
              },
              "workloadIdentity": {
                "type": "object",
                "properties": {
                  "workloadPool": { "type": "string", "pattern": "^[a-z0-9-]+\\.svc\\.id\\.goog$" }
                },
                "additionalProperties": false
              },
              "shieldedNodes": { "type": "boolean", "default": true },
              "logging": {
                "type": "object",
                "properties": {
                  "components": {
                    "type": "array",
                    "items": { "type": "string", "enum": ["SYSTEM_COMPONENTS", "WORKLOADS", "APISERVER", "SCHEDULER", "CONTROLLER_MANAGER"] }
                  }
                },
                "additionalProperties": false
              },
              "monitoring": {
                "type": "object",
                "properties": { "managedPrometheus": { "type": "boolean", "default": true } },
                "additionalProperties": false
              },
              "networkDataplane": { "type": "string", "enum": ["ADVANCED_DATAPATH", "DATAPATH_PROVIDER_UNSPECIFIED"], "default": "DATAPATH_PROVIDER_UNSPECIFIED" },
              "maintenanceWindow": { "type": "string", "description": "RFC3339 or recurring window notation" },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "createClusterAutopilot" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "name", "networkUri", "subnetworkUri"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
              "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "subnetworkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
              "releaseChannel": { "type": "string", "enum": ["RAPID", "REGULAR", "STABLE"], "default": "REGULAR" },
              "workloadIdentity": {
                "type": "object",
                "properties": { "workloadPool": { "type": "string", "pattern": "^[a-z0-9-]+\\.svc\\.id\\.goog$" } },
                "additionalProperties": false
              },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getCluster" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "listClusters" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteCluster" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "createNodePool" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "cluster", "name", "config"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "cluster": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeNodePoolName" },
              "size": {
                "type": "object",
                "properties": { "initial": { "type": "integer", "minimum": 0, "default": 1 } },
                "additionalProperties": false
              },
              "autoscaling": {
                "type": "object",
                "properties": {
                  "enabled": { "type": "boolean", "default": true },
                  "min": { "type": "integer", "minimum": 0, "default": 1 },
                  "max": { "type": "integer", "minimum": 1, "default": 5 }
                },
                "additionalProperties": false
              },
              "config": {
                "type": "object",
                "required": ["machineType"],
                "properties": {
                  "machineType": { "type": "string" },
                  "diskType": { "type": "string", "enum": ["pd-standard", "pd-balanced", "pd-ssd"], "default": "pd-balanced" },
                  "diskSizeGb": { "type": "integer", "minimum": 10, "default": 100 },
                  "imageType": { "type": "string", "default": "COS_CONTAINERD" },
                  "serviceAccount": { "type": "string" },
                  "preemptible": { "type": "boolean", "default": false },
                  "spot": { "type": "boolean", "default": false },
                  "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" },
                  "tags": { "type": "array", "items": { "type": "string" } },
                  "taints": {
                    "type": "array",
                    "items": {
                      "type": "object",
                      "required": ["key", "value", "effect"],
                      "properties": {
                        "key": { "type": "string" },
                        "value": { "type": "string" },
                        "effect": { "type": "string", "enum": ["NO_SCHEDULE", "PREFER_NO_SCHEDULE", "NO_EXECUTE"] }
                      },
                      "additionalProperties": false
                    }
                  },
                  "shieldedInstanceConfig": {
                    "type": "object",
                    "properties": { "enableSecureBoot": { "type": "boolean", "default": true } },
                    "additionalProperties": false
                  }
                },
                "additionalProperties": false
              },
              "management": {
                "type": "object",
                "properties": {
                  "autoUpgrade": { "type": "boolean", "default": true },
                  "autoRepair": { "type": "boolean", "default": true }
                },
                "additionalProperties": false
              },
              "version": { "type": "string" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "getNodePool" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "cluster", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "cluster": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeNodePoolName" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "listNodePools" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "cluster"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "cluster": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "deleteNodePool" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "location", "cluster", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
              "cluster": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeNodePoolName" }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/gke.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.gke Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Cluster" },
        { "$ref": "#/$defs/NodePool" },
        {
          "type": "object",
          "required": ["items"],
          "properties": { "items": { "type": "array", "items": { "anyOf": [{ "$ref": "#/$defs/Cluster" }, { "$ref": "#/$defs/NodePool" }] } } },
          "additionalProperties": false
        },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Cluster": {
      "type": "object",
      "required": ["projectId", "location", "name", "networkUri", "subnetworkUri", "status"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
        "networkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "subnetworkUri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
        "autopilotEnabled": { "type": "boolean", "default": false },
        "privateCluster": { "type": "boolean", "default": false },
        "workloadIdentityPool": { "type": "string" },
        "releaseChannel": { "type": "string", "enum": ["RAPID", "REGULAR", "STABLE"] },
        "ipAllocationPolicy": {
          "type": "object",
          "properties": {
            "clusterSecondaryRangeName": { "type": "string" },
            "servicesSecondaryRangeName": { "type": "string" },
            "stackType": { "type": "string", "enum": ["IPV4_ONLY", "IPV4_IPV6"] }
          }
        },
        "endpoint": { "type": "string" },
        "currentMasterVersion": { "type": "string" },
        "currentNodeVersion": { "type": "string" },
        "status": { "type": "string" }
      },
      "additionalProperties": true
    },
    "NodePool": {
      "type": "object",
      "required": ["projectId", "location", "cluster", "name"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "location": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Location" },
        "cluster": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeClusterName" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/GkeNodePoolName" },
        "version": { "type": "string" },
        "initialNodeCount": { "type": "integer" },
        "autoscaling": { "type": "object" },
        "config": { "type": "object" },
        "management": { "type": "object" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 5) `gcp.vpcaccess` — **Serverless VPC Access connectors**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/vpcaccess.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.vpcaccess Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": { "type": "string", "enum": ["ensureConnector", "getConnector", "deleteConnector"] },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "ensureConnector" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "network"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "network": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
              "subnet": {
                "type": "object",
                "properties": { "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" } },
                "additionalProperties": false
              },
              "minThroughput": { "type": "integer", "minimum": 200 },
              "maxThroughput": { "type": "integer", "minimum": 200 }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getConnector" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteConnector" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/vpcaccess.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.vpcaccess Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Connector" },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Connector": {
      "type": "object",
      "required": ["projectId", "region", "name", "network", "state"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "network": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ComputeName63" },
        "subnet": { "type": "object", "properties": { "name": { "type": "string" } } },
        "state": { "type": "string" },
        "minThroughput": { "type": "integer" },
        "maxThroughput": { "type": "integer" },
        "uri": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 6) `gcp.cloudrun` — **Cloud Run (services, IAM, traffic)**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/cloudrun.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.cloudrun Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": ["deployService", "getService", "listServices", "deleteService", "setIamPolicyInvoker", "splitTraffic"]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "deployService" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "image"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" },
              "image": { "type": "string" },
              "containerPort": { "type": "integer", "minimum": 1, "maximum": 65535, "default": 8080 },
              "env": {
                "type": "array",
                "items": {
                  "oneOf": [
                    {
                      "type": "object",
                      "required": ["name", "value"],
                      "properties": { "name": { "type": "string" }, "value": { "type": "string" } },
                      "additionalProperties": false
                    },
                    {
                      "type": "object",
                      "required": ["name", "secret"],
                      "properties": {
                        "name": { "type": "string" },
                        "secret": {
                          "type": "object",
                          "required": ["name"],
                          "properties": { "name": { "type": "string" }, "version": { "type": "string", "default": "latest" } },
                          "additionalProperties": false
                        }
                      },
                      "additionalProperties": false
                    }
                  ]
                },
                "default": []
              },
              "serviceAccount": { "type": "string" },
              "concurrency": { "type": "integer", "minimum": 1, "default": 80 },
              "scaling": {
                "type": "object",
                "properties": {
                  "min": { "type": "integer", "minimum": 0, "default": 0 },
                  "max": { "type": "integer", "minimum": 1, "default": 100 }
                },
                "additionalProperties": false
              },
              "cpu": { "type": "string", "default": "1" },
              "memory": { "type": "string", "pattern": "^\\d+(Mi|Gi)$", "default": "512Mi" },
              "ingress": {
                "type": "string",
                "enum": ["INGRESS_TRAFFIC_ALL", "INGRESS_TRAFFIC_INTERNAL_ONLY", "INGRESS_TRAFFIC_INTERNAL_LOAD_BALANCER"],
                "default": "INGRESS_TRAFFIC_ALL"
              },
              "vpcAccess": {
                "type": "object",
                "properties": {
                  "connector": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ResourceUri" },
                  "egress": { "type": "string", "enum": ["PRIVATE_RANGES_ONLY", "ALL_TRAFFIC"], "default": "PRIVATE_RANGES_ONLY" }
                },
                "additionalProperties": false
              },
              "executionEnvironment": { "type": "string", "enum": ["EXECUTION_ENVIRONMENT_GEN2"], "default": "EXECUTION_ENVIRONMENT_GEN2" },
              "traffic": {
                "type": "array",
                "description": "Percentages should sum to 100 (validated at runtime).",
                "items": {
                  "type": "object",
                  "properties": {
                    "latest": { "type": "boolean" },
                    "revision": { "type": "string" },
                    "percent": { "type": "integer", "minimum": 0, "maximum": 100 }
                  },
                  "additionalProperties": false
                }
              },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "getService" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "listServices" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "deleteService" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "setIamPolicyInvoker" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "members"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" },
              "members": {
                "type": "array",
                "items": { "type": "string" },
                "description": "e.g., allUsers, user:alice@example.com"
              }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "splitTraffic" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "region", "name", "traffic"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
              "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" },
              "traffic": {
                "type": "array",
                "items": {
                  "type": "object",
                  "properties": {
                    "latest": { "type": "boolean" },
                    "revision": { "type": "string" },
                    "percent": { "type": "integer", "minimum": 0, "maximum": 100 }
                  },
                  "additionalProperties": false
                }
              }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/cloudrun.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.cloudrun Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Service" },
        {
          "type": "object",
          "required": ["items"],
          "properties": { "items": { "type": "array", "items": { "$ref": "#/$defs/Service" } } },
          "additionalProperties": false
        },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Service": {
      "type": "object",
      "required": ["projectId", "region", "name", "uri"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "region": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Region" },
        "name": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/RunServiceName" },
        "uri": { "type": "string" },
        "ingress": { "type": "string" },
        "template": {
          "type": "object",
          "properties": {
            "image": { "type": "string" },
            "serviceAccount": { "type": "string" },
            "containerConcurrency": { "type": "integer" },
            "scaling": { "type": "object", "properties": { "minInstanceCount": { "type": "integer" }, "maxInstanceCount": { "type": "integer" } } }
          }
        },
        "traffic": {
          "type": "array",
          "items": { "type": "object", "properties": { "revision": { "type": "string" }, "percent": { "type": "integer" } } }
        },
        "latestReadyRevision": { "type": "string" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

## 7) `gcp.pubsub` — **Topics, Subscriptions, Publish**

### Request

```json
{
  "$id": "https://schemas.example.com/gcp/pubsub.request.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.pubsub Request",
  "type": "object",
  "required": ["op", "params"],
  "properties": {
    "op": {
      "type": "string",
      "enum": ["createTopic", "getTopic", "listTopics", "deleteTopic", "createSubscription", "getSubscription", "listSubscriptions", "deleteSubscription", "publish"]
    },
    "params": { "type": "object" }
  },
  "allOf": [
    {
      "if": { "properties": { "op": { "const": "createTopic" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "topicId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "topicId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" },
              "kmsKeyName": { "type": "string" },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getTopic" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "topicId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "topicId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "listTopics" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId"],
            "properties": { "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" } },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteTopic" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "topicId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "topicId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "createSubscription" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "subscriptionId", "topicId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "subscriptionId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" },
              "topicId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" },
              "pushConfig": {
                "type": "object",
                "properties": {
                  "pushEndpoint": { "type": "string", "format": "uri" },
                  "oidcToken": {
                    "type": "object",
                    "properties": { "serviceAccountEmail": { "type": "string" }, "audience": { "type": "string" } },
                    "additionalProperties": false
                  }
                },
                "additionalProperties": false
              },
              "ackDeadlineSeconds": { "type": "integer", "minimum": 10, "maximum": 600, "default": 10 },
              "retainAckedMessages": { "type": "boolean", "default": false },
              "messageRetentionDuration": { "type": "string", "description": "e.g., 604800s for 7 days" },
              "deadLetterPolicy": {
                "type": "object",
                "properties": { "deadLetterTopic": { "type": "string" }, "maxDeliveryAttempts": { "type": "integer", "minimum": 5, "maximum": 100 } },
                "additionalProperties": false
              },
              "exactlyOnceDelivery": { "type": "boolean", "default": false },
              "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "getSubscription" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "subscriptionId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "subscriptionId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" }
            },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "listSubscriptions" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId"],
            "properties": { "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" } },
            "additionalProperties": false
          }
        }
      }
    },
    {
      "if": { "properties": { "op": { "const": "deleteSubscription" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "subscriptionId"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "subscriptionId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" }
            },
            "additionalProperties": false
          }
        }
      }
    },

    {
      "if": { "properties": { "op": { "const": "publish" } } },
      "then": {
        "properties": {
          "params": {
            "type": "object",
            "required": ["projectId", "topicId", "messages"],
            "properties": {
              "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
              "topicId": { "type": "string", "pattern": "^[A-Za-z][A-Za-z0-9\\-_~%.+]{2,255}$" },
              "messages": {
                "type": "array",
                "minItems": 1,
                "items": {
                  "type": "object",
                  "required": ["data"],
                  "properties": {
                    "data": { "type": "string", "description": "Base64-encoded message body" },
                    "attributes": { "type": "object", "additionalProperties": { "type": "string" } },
                    "orderingKey": { "type": "string" }
                  },
                  "additionalProperties": false
                }
              }
            },
            "additionalProperties": false
          }
        }
      }
    }
  ],
  "additionalProperties": false
}
```

### Response

```json
{
  "$id": "https://schemas.example.com/gcp/pubsub.response.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "gcp.pubsub Response",
  "type": "object",
  "required": ["op", "result"],
  "properties": {
    "op": { "type": "string" },
    "result": {
      "oneOf": [
        { "$ref": "#/$defs/Topic" },
        { "$ref": "#/$defs/Subscription" },
        {
          "type": "object",
          "required": ["messageIds"],
          "properties": { "messageIds": { "type": "array", "items": { "type": "string" } } },
          "additionalProperties": false
        },
        {
          "type": "object",
          "required": ["items"],
          "properties": { "items": { "type": "array", "items": { "anyOf": [{ "$ref": "#/$defs/Topic" }, { "$ref": "#/$defs/Subscription" }] } } },
          "additionalProperties": false
        },
        { "type": "null" }
      ]
    }
  },
  "$defs": {
    "Topic": {
      "type": "object",
      "required": ["projectId", "topicId", "name"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "topicId": { "type": "string" },
        "name": { "type": "string", "description": "Full resource path: projects/{project}/topics/{topic}" },
        "kmsKeyName": { "type": "string" },
        "labels": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/Labels" }
      },
      "additionalProperties": true
    },
    "Subscription": {
      "type": "object",
      "required": ["projectId", "subscriptionId", "name", "topic"],
      "properties": {
        "projectId": { "$ref": "https://schemas.example.com/gcp/common.json#/$defs/ProjectId" },
        "subscriptionId": { "type": "string" },
        "name": { "type": "string", "description": "projects/{project}/subscriptions/{subscription}" },
        "topic": { "type": "string", "description": "projects/{project}/topics/{topic}" },
        "ackDeadlineSeconds": { "type": "integer" },
        "retainAckedMessages": { "type": "boolean" },
        "messageRetentionDuration": { "type": "string" },
        "exactlyOnceDelivery": { "type": "boolean" },
        "state": { "type": "string" }
      },
      "additionalProperties": true
    }
  },
  "additionalProperties": false
}
```

---

### How to use

* Validate a call with the relevant **request schema** before sending it to your executor.
* Your executor calls the real Google APIs (polling LROs where applicable), then normalize to the **response schema**.
* Cross‑plugin checks (e.g., GKE Standard requires named secondary ranges; private clusters require NAT) can be enforced either in a higher‑level planner or directly before execution.

---

If you want, I can bundle these into a **single file with `$defs` per plugin**, or add **JSON Examples** for each operation so you can wire up end‑to‑end tests quickly.
