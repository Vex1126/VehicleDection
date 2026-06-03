#pragma once

#include <map>
#include <string>

namespace vehicle::infra {

struct HttpResponse {
    int statusCode{0};
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
};

class HttpClient {
public:
    [[nodiscard]] HttpResponse postJson(const std::string& url,
                                        const std::string& jsonBody,
                                        long timeoutMs = 1000) const;
};

} // namespace vehicle::infra
