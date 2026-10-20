#include "net/http.h"

#include "version.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>
#else
#include <dlfcn.h>

#include <mutex>
#endif

namespace geow2 {

namespace {
const std::string kUserAgent = std::string("geow2/") + GEOW2_VERSION;
// The largest answer read: a sync's reply is well under a megabyte.
constexpr size_t kMaxBody = 8u << 20;
const char* const kTooLarge = "the server's answer was too large";
}

#ifdef _WIN32

namespace {

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string last_error(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

}  // namespace

HttpResponse http_request(const std::string& url, const std::string& body, const std::string& bearer,
                          int timeout_seconds) {
  HttpResponse r;
  std::wstring wurl = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256] = {}, path[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
    r.error = "not a web address: " + url;
    return r;
  }
  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  Handle session{WinHttpOpen(wide(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.h) {
    r.error = last_error("WinHttpOpen");
    return r;
  }
  const int ms = timeout_seconds * 1000;
  WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
  Handle connect{WinHttpConnect(session.h, host, parts.nPort, 0)};
  if (!connect.h) {
    r.error = last_error("WinHttpConnect");
    return r;
  }
  Handle request{WinHttpOpenRequest(connect.h, body.empty() ? L"GET" : L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
  if (!request.h) {
    r.error = last_error("WinHttpOpenRequest");
    return r;
  }
  // No redirects (WinHTTP follows them by default): the token goes only to
  // the address given.
  DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
  std::wstring headers = L"Accept: application/json\r\n";
  if (!body.empty()) headers += L"Content-Type: application/json\r\n";
  if (!bearer.empty()) headers += L"Authorization: Bearer " + wide(bearer) + L"\r\n";
  if (!WinHttpSendRequest(request.h, headers.c_str(), DWORD(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                          DWORD(body.size()), DWORD(body.size()), 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    const DWORD e = GetLastError();
    r.error = e == ERROR_WINHTTP_TIMEOUT                ? "the server did not answer in time"
              : e == ERROR_WINHTTP_NAME_NOT_RESOLVED    ? "server not found"
              : e == ERROR_WINHTTP_CANNOT_CONNECT       ? "cannot connect to the server"
              : e == ERROR_WINHTTP_SECURE_FAILURE       ? "the server's certificate was refused"
                                                        : "the request failed (error " + std::to_string(e) + ")";
    return r;
  }
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
    if (r.body.size() + available > kMaxBody) {
      r.body.clear();
      r.error = kTooLarge;
      return r;
    }
    std::vector<char> chunk(available);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, chunk.data(), available, &read) || read == 0) break;
    r.body.append(chunk.data(), read);
  }
  r.status = int(status);
  return r;
}

#else

namespace {

// The few parts of libcurl's API used here, with its stable option
// numbers (curl/curl.h), so no curl headers are needed to build.
using Curl = void;
struct CurlList;
using EasyInit = Curl* (*)();
using EasySetopt = int (*)(Curl*, int, ...);
using EasyPerform = int (*)(Curl*);
using EasyGetinfo = int (*)(Curl*, int, ...);
using EasyCleanup = void (*)(Curl*);
using EasyStrerror = const char* (*)(int);
using SlistAppend = CurlList* (*)(CurlList*, const char*);
using SlistFree = void (*)(CurlList*);
using GlobalInit = int (*)(long);

constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptPostFields = 10015, kOptUserAgent = 10018,
              kOptHttpHeader = 10023, kOptWriteFunction = 20011, kOptTimeout = 13, kOptConnectTimeout = 78,
              kOptNoSignal = 99, kOptPostFieldSize = 60, kOptProtocolsStr = 10318;
constexpr int kInfoResponseCode = 0x200002;
constexpr long kGlobalAll = 3;

struct Lib {
  EasyInit init = nullptr;
  EasySetopt setopt = nullptr;
  EasyPerform perform = nullptr;
  EasyGetinfo getinfo = nullptr;
  EasyCleanup cleanup = nullptr;
  EasyStrerror strerror = nullptr;
  SlistAppend append = nullptr;
  SlistFree free_list = nullptr;
  bool ok = false;
};

const Lib& curl() {
  static Lib lib;
  static std::once_flag once;
  std::call_once(once, [] {
    static const char* const kNames[] = {
#ifdef __APPLE__
        "/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib",
#else
        "libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so",
#endif
    };
    void* h = nullptr;
    for (const char* n : kNames)
      if ((h = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
    if (!h) return;
    auto sym = [h](const char* name) { return dlsym(h, name); };
    if (auto g = reinterpret_cast<GlobalInit>(sym("curl_global_init"))) g(kGlobalAll);
    lib.init = reinterpret_cast<EasyInit>(sym("curl_easy_init"));
    lib.setopt = reinterpret_cast<EasySetopt>(sym("curl_easy_setopt"));
    lib.perform = reinterpret_cast<EasyPerform>(sym("curl_easy_perform"));
    lib.getinfo = reinterpret_cast<EasyGetinfo>(sym("curl_easy_getinfo"));
    lib.cleanup = reinterpret_cast<EasyCleanup>(sym("curl_easy_cleanup"));
    lib.strerror = reinterpret_cast<EasyStrerror>(sym("curl_easy_strerror"));
    lib.append = reinterpret_cast<SlistAppend>(sym("curl_slist_append"));
    lib.free_list = reinterpret_cast<SlistFree>(sym("curl_slist_free_all"));
    lib.ok = lib.init && lib.setopt && lib.perform && lib.getinfo && lib.cleanup && lib.strerror && lib.append &&
             lib.free_list;
  });
  return lib;
}

// The answer, up to kMaxBody; past it the transfer stops (returning less
// than given makes libcurl fail the request).
struct Body {
  std::string text;
  bool too_large = false;
};

size_t collect(char* data, size_t size, size_t n, void* out) {
  Body& b = *static_cast<Body*>(out);
  if (b.text.size() + size * n > kMaxBody) {
    b.too_large = true;
    return 0;
  }
  b.text.append(data, size * n);
  return size * n;
}

}  // namespace

HttpResponse http_request(const std::string& url, const std::string& body, const std::string& bearer,
                          int timeout_seconds) {
  HttpResponse r;
  const Lib& c = curl();
  if (!c.ok) {
    r.error = "libcurl was not found on this system";
    return r;
  }
  Curl* h = c.init();
  if (!h) {
    r.error = "libcurl could not start";
    return r;
  }
  CurlList* headers = c.append(nullptr, "Accept: application/json");
  if (!body.empty()) headers = c.append(headers, "Content-Type: application/json");
  const std::string auth = "Authorization: Bearer " + bearer;
  if (!bearer.empty()) headers = c.append(headers, auth.c_str());
  c.setopt(h, kOptUrl, url.c_str());
  c.setopt(h, kOptProtocolsStr, "https,http");
  c.setopt(h, kOptUserAgent, kUserAgent.c_str());
  c.setopt(h, kOptHttpHeader, headers);
  c.setopt(h, kOptNoSignal, 1L);
  c.setopt(h, kOptTimeout, long(timeout_seconds));
  c.setopt(h, kOptConnectTimeout, long(timeout_seconds));
  // (Redirects are not followed: libcurl's default.)
  Body received;
  c.setopt(h, kOptWriteFunction, &collect);
  c.setopt(h, kOptWriteData, &received);
  if (!body.empty()) {
    c.setopt(h, kOptPostFieldSize, long(body.size()));
    c.setopt(h, kOptPostFields, body.c_str());
  }
  const int rc = c.perform(h);
  if (rc == 0) {
    long status = 0;
    c.getinfo(h, kInfoResponseCode, &status);
    r.status = int(status);
    r.body = std::move(received.text);
  } else {
    r.error = received.too_large ? kTooLarge : c.strerror(rc);
  }
  c.free_list(headers);
  c.cleanup(h);
  return r;
}

#endif

}  // namespace geow2
