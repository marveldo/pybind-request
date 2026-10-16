#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <format>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pybind11/embed.h>

using std::string;
using std::cin;
using std::cout;
using std::format;
using std::optional;
using std::nullopt;
using std::endl;
using std::to_string;
namespace py = pybind11;
using std::runtime_error;

WSADATA wsadata;
const int DEFAULT_BBUFFER_SIZE = 1024;

struct HttpSplitUnit {
    string host;
    optional<string> path;
    optional<int> port;
};

// Function for extracting the components from the Url
HttpSplitUnit* extractPureDomain(string rawUrl) { // Passed by value so we don't destroy the user's original string
    optional<int> port = nullopt;
    optional<string> path = nullopt;

    // 1. Strip the protocol prefix (e.g., "https://" or "http://")
    size_t protocolPos = rawUrl.find("://");
    if (protocolPos != string::npos) {
        rawUrl = rawUrl.substr(protocolPos + 3);
    }

    // Copy everything from the path down into a new Variable
    size_t pathPos = rawUrl.find("/");
    if (pathPos != string::npos) {
        
        path = rawUrl.substr(pathPos);
     
        rawUrl = rawUrl.substr(0, pathPos);
    }

  // Copy the port and transform it to an int 
    size_t portPos = rawUrl.find(":");
    if (portPos != string::npos) {
        
        string portStr = rawUrl.substr(portPos + 1);
         

       // Convert the port to an Int if port is part 
        if (!portStr.empty()) {
            port = stoi(portStr);
        }
        
        // Assign it to rawUrl
        rawUrl = rawUrl.substr(0, portPos);
    }

    // 4. Return the heap-allocated struct matching your pointer return type
    HttpSplitUnit* result = new HttpSplitUnit();
    result->host = rawUrl;
    result->path = path;
    result->port = port;
    
    return result;
}
int initializeWin() {
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
    printf("StartUp successful\n");
    return 0;
}


addrinfo* GetDomainAddressInfo(const HttpSplitUnit* domain) {
    addrinfo* results = nullptr;
    addrinfo hints = {};
    int port = domain->port.value_or(80);
    string portString = to_string(port);
    
    hints.ai_family   = AF_UNSPEC;   // Support IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP continuous stream
    hints.ai_protocol = IPPROTO_TCP; // Explicit TCP protocol
    
    // Pass "443" (HTTPS) so the address structure configures the correct port
    int iResult = getaddrinfo(domain->host.c_str(), portString.c_str() , &hints, &results);
    
    if (iResult != 0) {
        cout << "Error: Could not resolve domain address.\n";
        return nullptr;
    }

    return results;
}

string GenerateHttpRequestString(const HttpSplitUnit* domain , string method ){
    string path_str = domain->path.value_or("/");
    string host_str = domain->host;
    string httpReturn;

    if (domain->port.has_value() && domain->port.value() != 443 && domain->port.value() != 80)
    {
        host_str += ":" + to_string(domain->port.value());
    }

    httpReturn =
        format(
            "{} {} HTTP/1.1\r\n"
            "Host: {}\r\n"
            "User-Agent: C++ HTTPS Client\r\n"
            "Connection: close\r\n\r\n",
            method,
            path_str,
            host_str);
   
    return httpReturn;
}

void sendhttpdata(SOCKET& connectedSocket , string* httpRequest )
{
    SOCKET iResult = send(connectedSocket , httpRequest->c_str(), (int) strlen(httpRequest->c_str()), 0);
    if(iResult == SOCKET_ERROR){
        printf("Get Last Error %d\n", WSAGetLastError());
        closesocket(connectedSocket);
        WSACleanup();
    }
}

// function to receive http data over socket connection

string receivehttpData(SOCKET& connectedSocket) {
    string fullResponse;
    char tempChunk[DEFAULT_BBUFFER_SIZE];
    int bytesReceived = 0; 

    do {
        // 1. Read directly into bytesReceived
        bytesReceived = recv(connectedSocket, tempChunk, sizeof(tempChunk), 0);
        
        if (bytesReceived > 0) {
            printf("Bytes received: %d\n", bytesReceived);
            fullResponse.append(tempChunk, bytesReceived);
        } 
        else if (bytesReceived == 0) {
            printf("Connection closed by server.\n");
            break;
        } 
        else {
            std::cerr << "recv failed: " << WSAGetLastError() << "\n";
            break;
        }

    } while (bytesReceived > 0); 
    
    return fullResponse;
}

optional<SOCKET> CreateClientSocket(addrinfo* addr_info) {
    if (!addr_info) return nullopt;

    SOCKET ConnectSocket = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Error at socket(): %ld\n", WSAGetLastError());
        return nullopt;
    }
    return ConnectSocket;
}

int ConnecttoSocket(SOCKET& clientSocket, addrinfo* addr_info) {   
    if (!addr_info) return 1;

    int iResult = connect(clientSocket, addr_info->ai_addr, (int)addr_info->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("Connection failed with error: %ld\n", WSAGetLastError());
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        return 1;
    }
    return 0;
}

// Sets up OpenSSL and tells it to check certificates against the Windows certificate store
SSL_CTX* CreateSSLContext() {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return nullptr;

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_load_verify_store(ctx, "org.openssl.winstore://") != 1) {
        printf("Could not load the Windows certificate store\n");
        SSL_CTX_free(ctx);
        return nullptr;
    }
    return ctx;
}

// Does the TLS handshake on top of an already connected TCP socket
SSL* CreateSSLConnection(SSL_CTX* ctx, SOCKET& connectedSocket, const string& host) {
    SSL* ssl = SSL_new(ctx);
    if (!ssl) return nullptr;

    SSL_set_fd(ssl, (int)connectedSocket);
    SSL_set_tlsext_host_name(ssl, host.c_str()); // Tells the server which site we want (SNI)
#if OPENSSL_VERSION_MAJOR >= 4
    SSL_set1_dnsname(ssl, host.c_str());          // Certificate must belong to this host
#else
    SSL_set1_host(ssl, host.c_str());             // Same check for OpenSSL 3.x
#endif

    if (SSL_connect(ssl) != 1) {
        printf("TLS handshake failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return nullptr;
    }
    return ssl;
}

// Same as sendhttpdata but goes through the encrypted connection
void sendhttpsdata(SSL* ssl, string* httpRequest) {
    int iResult = SSL_write(ssl, httpRequest->c_str(), (int)httpRequest->size());
    if (iResult <= 0) {
        printf("SSL_write failed\n");
        ERR_print_errors_fp(stderr);
    }
}

// Same as receivehttpData but reads from the encrypted connection
string receivehttpsData(SSL* ssl) {
    string fullResponse;
    char tempChunk[DEFAULT_BBUFFER_SIZE];
    int bytesReceived = 0;

    do {
        bytesReceived = SSL_read(ssl, tempChunk, sizeof(tempChunk));

        if (bytesReceived > 0) {
            printf("Bytes received: %d\n", bytesReceived);
            fullResponse.append(tempChunk, bytesReceived);
        }
    } while (bytesReceived > 0);

    return fullResponse;
}

// Some servers send the body in pieces: "<size in hex>\r\n<data>\r\n" ... ending with "0\r\n\r\n"
string decodeChunkedBody(const string& body) {
    string decoded;
    size_t pos = 0;

    while (pos < body.size()) {
        size_t lineEnd = body.find("\r\n", pos);
        if (lineEnd == string::npos || lineEnd == pos) break;

        int chunkSize = stoi(body.substr(pos, lineEnd - pos), nullptr, 16);
        if (chunkSize == 0) break;

        decoded += body.substr(lineEnd + 2, chunkSize);
        pos = lineEnd + 2 + chunkSize + 2; // Skip the data and the \r\n after it
    }
    return decoded;
}

// Builds the dict we hand back when something goes wrong
py::dict ErrorDict(const string& message) {
    py::dict result;
    result["ok"] = false;
    result["error"] = message;
    return result;
}

// Turns the raw HTTP response into a Python dict with the status, headers and body
py::dict ParseHttpResponse(const string& rawResponse) {
    size_t headerEnd = rawResponse.find("\r\n\r\n");
    if (headerEnd == string::npos) return ErrorDict("Malformed HTTP response");

    string headerPart = rawResponse.substr(0, headerEnd);
    string body = rawResponse.substr(headerEnd + 4);

    // Status line looks like "HTTP/1.1 404 Not Found"
    size_t firstLineEnd = headerPart.find("\r\n");
    string statusLine = headerPart.substr(0, firstLineEnd);
    size_t codePos = statusLine.find(" ");
    if (codePos == string::npos) return ErrorDict("Malformed status line");
    int statusCode = stoi(statusLine.substr(codePos + 1, 3));

    // Every other header line looks like "Name: value"
    py::dict headers;
    string transferEncoding;
    size_t pos = firstLineEnd;
    while (pos != string::npos && pos < headerPart.size()) {
        size_t lineStart = pos + 2;
        size_t lineEnd = headerPart.find("\r\n", lineStart);
        string line = headerPart.substr(lineStart, lineEnd - lineStart);

        size_t colonPos = line.find(":");
        if (colonPos != string::npos) {
            string name = line.substr(0, colonPos);
            string value = line.substr(colonPos + 1);
            if (!value.empty() && value[0] == ' ') value = value.substr(1);

            // Header names are case insensitive so store them lowercase
            for (size_t i = 0; i < name.size(); i++) name[i] = tolower(name[i]);
            headers[py::str(name)] = value;
            if (name == "transfer-encoding") transferEncoding = value;
        }
        pos = lineEnd;
    }

    if (transferEncoding.find("chunked") != string::npos) {
        body = decodeChunkedBody(body);
    }

    py::dict result;
    result["status"] = statusCode;
    result["headers"] = headers;
    // Body goes back as bytes (like requests' .content) since it is not always valid text
    result["body"] = py::bytes(body);

    // 4XX means the request was wrong (bad url, not found, not allowed...)
    if (statusCode >= 400 && statusCode < 500) {
        result["ok"] = false;
        result["error"] = format("Client error: {}", statusLine);
    } else {
        result["ok"] = true;
    }
    return result;
}

// Runs the full request flow for a given url and method and returns the result as a Python dict
py::dict RunHttpRequest(const string& url, const string& method) {
    string response;
    string error;
    HttpSplitUnit* domain =  extractPureDomain(url);

    // https:// means an encrypted connection, which uses port 443 instead of 80
    bool useSSL = url.find("https://") == 0;
    if (useSSL && !domain->port.has_value()) {
        domain->port = 443;
    }

    int result = initializeWin();
    if (result != 0) {
        delete domain;
        return ErrorDict("WSAStartup failed");
    }

    // Get address info as a POINTER
    addrinfo* addressinfo = GetDomainAddressInfo(domain);
    if (!addressinfo) {
        WSACleanup();
        string message = "Could not resolve domain " + domain->host;
        delete domain;
        return ErrorDict(message);
    }

    char ipString[INET6_ADDRSTRLEN];
    int conversionResult = getnameinfo(
        addressinfo->ai_addr,
        addressinfo->ai_addrlen,
        ipString,
        sizeof(ipString),
        nullptr, 0,
        NI_NUMERICHOST
    );

    if (conversionResult == 0) {
        cout << format("Address IP : {}\n", ipString);
    } else {
        cout << "Failed to convert address structure to string.\n";
    }


    auto clientSocketOpt = CreateClientSocket(addressinfo);
    if (clientSocketOpt.has_value()) {
        SOCKET clientSocket = clientSocketOpt.value();
        cout << "Socket successfully created!\n";

        if (ConnecttoSocket(clientSocket, addressinfo) == 0) {
            cout << "Successfully connected TCP Handshake under the hood!\n";

            string httpRequest = GenerateHttpRequestString(domain , method);

            if (useSSL) {
                SSL_CTX* ctx = CreateSSLContext();
                if (!ctx) {
                    error = "Could not set up OpenSSL";
                } else {
                    SSL* ssl = CreateSSLConnection(ctx, clientSocket, domain->host);
                    if (!ssl) {
                        error = "TLS handshake failed";
                    } else {
                        cout << "TLS handshake done, connection is encrypted!\n";
                        sendhttpsdata(ssl, &httpRequest);
                        response = receivehttpsData(ssl);
                        SSL_shutdown(ssl);
                        SSL_free(ssl);
                    }
                    SSL_CTX_free(ctx);
                }
            } else {
                sendhttpdata(clientSocket , &httpRequest);
                response = receivehttpData(clientSocket);
            }
            closesocket(clientSocket);
        } else {
            error = "Could not connect to " + domain->host;
        }
    } else {
        error = "Could not create socket";
    }

    freeaddrinfo(addressinfo);
    WSACleanup();


    cout << format("URL: {}\n Host : {}\n  Port: {}\n Path : {}\n Method : {}\n", url , domain->host , domain->port.value_or(443), domain->path.value_or("/"), method);
    delete domain;

    if (!error.empty()) return ErrorDict(error);
    if (response.empty()) return ErrorDict("Server sent back nothing");
    return ParseHttpResponse(response);
}

int main() {
    py::scoped_interpreter guard{}; // Starts Python so pybind objects work in a normal exe

    string url;
    string method ;
    cout << "Enter Your Url: ";
    cin >> url;
    cout << "Enter Your HTTP method";
    cin >> method;

    py::dict data = RunHttpRequest(url, method);
    py::print(data);
    return 0;
}

// Makes this file importable from Python as "import netcp"
PYBIND11_MODULE(netcp, m) {
    m.def("request", &RunHttpRequest, 
        "A request Function Built With C++",
        py::arg("url"), 
        py::arg("method") = "GET",
        py::return_value_policy::copy
     )  ;
}
