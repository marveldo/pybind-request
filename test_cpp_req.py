import os
import json

# Python does not search PATH for a module's DLLs, so point it at OpenSSL before importing
os.add_dll_directory(r"C:\Program Files\OpenSSL-Win64\bin")

import netcp

response = netcp.request("http://localhost:3000/api/v1/product")

if response["ok"]:
    print("Status:", response["status"])
    print("Content type:", response["headers"].get("content-type"))
    data = json.loads(response["body"])
    print("Data:", data)
else:
    print("Request failed:", response["error"])
