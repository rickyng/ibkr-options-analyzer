#pragma once

#include "result.hpp"
#include <string>
#include <map>
#include <chrono>
#include <functional>

namespace ibkr::utils {

/**
 * HTTP client wrapper around cpp-httplib with retry logic and exponential backoff.
 *
 * Provides a simple interface for making HTTP requests with automatic retries
 * for transient failures (network errors, timeouts, 5xx responses).
 *
 * Usage:
 *   HttpClient client("https://api.example.com", "MyApp/1.0");
 *   auto result = client.get("/endpoint", {{"param", "value"}});
 *   if (result) {
 *       std::cout << "Response: " << result->body << "\n";
 *   }
 */

struct HttpResponse {
    int status_code;
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpClient {
public:
    /**
     * Constructor
     * @param base_url Base URL (e.g., "https://api.example.com")
     * @param user_agent User-Agent header value
     * @param timeout_seconds Request timeout in seconds
     * @param max_retries Maximum number of retry attempts
     * @param initial_retry_delay_ms Initial retry delay in milliseconds
     */
    HttpClient(std::string base_url,
              std::string user_agent,
              int timeout_seconds = 30,
              int max_retries = 5,
              int initial_retry_delay_ms = 2000);

    /**
     * Make a GET request
     * @param path URL path (e.g., "/api/data")
     * @param params Query parameters
     * @return Result containing HttpResponse or Error
     */
    Result<HttpResponse> get(const std::string& path,
                            const std::map<std::string, std::string>& params = {});

    /**
     * Make a POST request
     * @param path URL path
     * @param body Request body
     * @param content_type Content-Type header
     * @return Result containing HttpResponse or Error
     */
    Result<HttpResponse> post(const std::string& path,
                             const std::string& body,
                             const std::string& content_type = "application/x-www-form-urlencoded");

    /**
     * Set custom headers for all requests
     */
    void set_header(const std::string& key, const std::string& value);

private:
    std::string base_url_;
    std::string user_agent_;
    int timeout_seconds_;
    int max_retries_;
    int initial_retry_delay_ms_;
    std::map<std::string, std::string> custom_headers_;

    /**
     * Execute request with retry logic
     * @param request_fn Function that performs the actual HTTP request
     * @return Result containing HttpResponse or Error
     */
    Result<HttpResponse> execute_with_retry(
        std::function<Result<HttpResponse>()> request_fn);

    /**
     * Check if error is retryable (network error, timeout, 5xx)
     */
    bool is_retryable_error(int status_code) const;

    /**
     * Calculate retry delay with exponential backoff
     */
    std::chrono::milliseconds calculate_retry_delay(int attempt) const;

    /**
     * Build query string from parameters
     */
    std::string build_query_string(const std::map<std::string, std::string>& params) const;

    /**
     * URL-encode a string using percent-encoding (RFC 3986)
     */
    std::string url_encode(const std::string& value) const;
};

} // namespace ibkr::utils
