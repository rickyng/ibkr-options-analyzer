#pragma once

#include "result.hpp"
#include <string>
#include <map>
#include <chrono>
#include <functional>
#include <memory>

namespace httplib {
    class Client;
}

namespace ibkr::utils {

struct HttpResponse {
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;
};

std::string url_encode(const std::string& value);

class HttpClient {
public:
    HttpClient(std::string base_url,
              std::string user_agent,
              int timeout_seconds = 30,
              int max_retries = 5,
              int initial_retry_delay_ms = 2000);

    ~HttpClient();

    Result<HttpResponse> get(const std::string& path,
                            const std::map<std::string, std::string>& params = {});

    Result<HttpResponse> post(const std::string& path,
                             const std::string& body,
                             const std::string& content_type = "application/x-www-form-urlencoded");

    void set_header(const std::string& key, const std::string& value);

private:
    std::string base_url_;
    std::string user_agent_;
    int timeout_seconds_;
    int max_retries_;
    int initial_retry_delay_ms_;
    std::map<std::string, std::string> custom_headers_;
    std::unique_ptr<httplib::Client> client_;

    Result<HttpResponse> execute_with_retry(
        std::function<Result<HttpResponse>()> request_fn);

    bool is_retryable_error(int status_code) const;
    std::chrono::milliseconds calculate_retry_delay(int attempt) const;
    std::string build_query_string(const std::map<std::string, std::string>& params) const;
};

} // namespace ibkr::utils
