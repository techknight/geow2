#pragma once

#include <string>

namespace geow2 {

struct HttpResponse {
  int status = 0;     // 0: no answer (see error)
  std::string body;
  std::string error;  // why there was no answer
};

// A blocking HTTPS (or plain HTTP, for a server on the local machine)
// request through the system's own library: WinHTTP on Windows, libcurl
// elsewhere, loaded when first needed so the game starts without it.
// `body` empty: GET; else POST with a JSON body. `bearer`, when set, goes
// in an Authorization header.
HttpResponse http_request(const std::string& url, const std::string& body, const std::string& bearer = {},
                          int timeout_seconds = 20);

}  // namespace geow2
