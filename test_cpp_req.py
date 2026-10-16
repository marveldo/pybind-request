import os
import json


import netcp

response = netcp.request("http://github.com")

if response["ok"]:
    print("Status:", response["status"])
    print("Content type:", response["headers"].get("content-type"))
    data = json.loads(response["body"])
    print("Data:", data)
else:
    print("Request failed:", response["error"])
