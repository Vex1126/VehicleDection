#include "vehicle/infra/http_client.hpp"

#if defined(VEHICLE_WITH_CURL)
#include <curl/curl.h>
#endif

namespace vehicle::infra {

namespace {

#if defined(VEHICLE_WITH_CURL)
std::size_t writeBody(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

#endif

} // namespace

HttpResponse HttpClient::postJson(const std::string& url, const std::string& jsonBody, long timeoutMs) const
{
#if defined(VEHICLE_WITH_CURL)
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.statusCode = 0;
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.statusCode = static_cast<int>(status);
    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
#else
    (void)url;
    (void)jsonBody;
    (void)timeoutMs;
    return {503, {}, {}, "libcurl is not available"};
#endif
}

} // namespace vehicle::infra
