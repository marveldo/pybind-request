# netcp

A small HTTP/HTTPS client written in C++ on top of Winsock and OpenSSL, exposed to Python as the `netcp` module via pybind11.

```python
import netcp

response = netcp.request("http://localhost:3000/api/v1/product")
if response["ok"]:
    print(response["status"], response["body"])
else:
    print("Request failed:", response["error"])
```

OpenSSL and the MinGW runtime are linked statically, so the built `netcp.cp314-win_amd64.pyd` is self-contained: no OpenSSL install, PATH changes or `os.add_dll_directory` needed to import it.

## Requirements

- Windows 64-bit
- Python 3.14 (the `.pyd` name and `-lpython314` in `build.bat` are tied to this version)
- MinGW-w64 g++ with C++20 support, on PATH
- Git (to install vcpkg)

## Setup

### 1. Install vcpkg

Clone it to a short path outside OneDrive (OneDrive will try to sync its thousands of files):

```cmd
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Optionally add it to PATH so `vcpkg` works from any folder, then open a new terminal:

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\vcpkg", "User")
```

vcpkg is not available on Chocolatey.

### 2. Build static OpenSSL with MinGW

```cmd
set VCPKG_MAX_CONCURRENCY=4
vcpkg install openssl:x64-mingw-static --host-triplet=x64-mingw-static
```

- `x64-mingw-static` builds with g++ instead of MSVC, so Visual Studio is not needed. A static library built with MSVC would not link into a g++ build anyway.
- `--host-triplet` stops vcpkg from building its helper packages with MSVC.
- `VCPKG_MAX_CONCURRENCY` limits parallel compile jobs. With the default (one per CPU thread), antivirus scanning can cause random "Invalid argument" errors when opening headers.

The first run downloads CMake, MSYS2 tools and Perl, then compiles OpenSSL. Expect 10-20 minutes. The result lands in `C:\vcpkg\installed\x64-mingw-static`.

### 3. Create the Python environment

```cmd
uv venv
uv pip install pybind11
```

(or `python -m venv .venv` and `.venv\Scripts\pip install pybind11`)

### 4. Build the module

From the project folder:

```cmd
build
```

This produces `netcp.cp314-win_amd64.pyd`. If your Python or vcpkg is installed somewhere else, edit the `PY` and `SSL` paths at the top of `build.bat`.

### 5. Test it

```cmd
.venv\Scripts\python test_cpp_req.py
```

`test_cpp_req.py` expects a server on `http://localhost:3000`.

## OpenSSL versions

The code builds against OpenSSL 3.x (what vcpkg currently ships) and 4.x. The one difference it handles is hostname verification: 4.x uses `SSL_set1_dnsname`, 3.x uses `SSL_set1_host`, selected with `OPENSSL_VERSION_MAJOR` in `CreateSSLConnection`.

## Troubleshooting

| Problem | Fix |
|---|---|
| `Unable to find a valid Visual Studio instance` | You used a `x64-windows*` triplet. Use `x64-mingw-static` with `--host-triplet=x64-mingw-static`. |
| `... .h: Invalid argument` during the vcpkg build | Lower `VCPKG_MAX_CONCURRENCY` (try 1), or exclude `C:\vcpkg` and your MinGW folder from Windows Defender. |
| `curl operation failed with error code 56` | Network hiccup. Rerun the command; finished downloads are cached. |
| `ImportError: DLL load failed` | The `.pyd` was built against DLL OpenSSL. Rebuild with `build.bat`. |
| `undefined reference` to `Crypt*` / `BCrypt*` / `WSA*` | A system library is missing from the link line. Keep the `-l` order in `build.bat`: `-lssl -lcrypto` first, Windows libraries after. |
