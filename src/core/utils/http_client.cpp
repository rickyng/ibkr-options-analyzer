#include "http_client.hpp"
#include "logger.hpp"
#include <httplib.h>
#include <thread>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace ibkr::utils {

namespace {
std::string httplib_error_to_string(httplib::Error err) {
    switch (err) {
        case httplib::Error::Connection: return "Connection failed";
        case httplib::Error::BindIPAddress: return "Failed to bind IP address";
        case httplib::Error::Read: return "Read error";
        case httplib::Error::Write: return "Write error";
        case httplib::Error::ExceedRedirectCount: return "Too many redirects";
        case httplib::Error::Canceled: return "Request canceled";
        case httplib::Error::SSLConnection: return "SSL connection failed";
        case httplib::Error::SSLLoadingCerts: return "Failed to load SSL certificates";
        case httplib::Error::SSLServerVerification: return "SSL server verification failed";
        case httplib::Error::UnsupportedMultipartBoundaryChars: return "Unsupported multipart boundary characters";
        case httplib::Error::Compression: return "Compression error";
        default: return "Unknown error";
    }
}
}

HttpClient::HttpClient(std::string base_url,
                      std::string user_agent,
                      int timeout_seconds,
                      int max_retries,
                      int initial_retry_delay_ms)
    : base_url_(std::move(base_url))
    , user_agent_(std::move(user_agent))
    , timeout_seconds_(timeout_seconds)
    , max_retries_(max_retries)
    , initial_retry_delay_ms_(initial_retry_delay_ms)
    , client_(std::make_unique<httplib::Client>(base_url_)) {
    client_->set_connection_timeout(timeout_seconds_);
    client_->set_read_timeout(timeout_seconds_);
    client_->set_write_timeout(timeout_seconds_);
}

HttpClient::~HttpClient() = default;

void HttpClient::set_header(const std::string& key, const std::string& value) {
    custom_headers_[key] = value;
}

Result<HttpResponse> HttpClient::get(const std::string& path,
                                     const std::map<std::string, std::string>& params) {
    std::string full_path = path;
    if (!params.empty()) {
        full_path += "?" + build_query_string(params);
    }

    Logger::debug("HTTP GET: {}{}", base_url_, full_path);

    return execute_with_retry([this, &full_path]() -> Result<HttpResponse> {
        httplib::Headers headers;
        headers.emplace("User-Agent", user_agent_);
        for (const auto& [key, value] : custom_headers_) {
            headers.emplace(key, value);
        }

        auto res = client_->Get(full_path, headers);

        if (!res) {
            auto err = res.error();
            return Error{httplib_error_to_string(err), base_url_ + full_path, static_cast<int>(err)};
        }

        HttpResponse response;
        response.status_code = res->status;
        response.body = res->body;
        for (const auto& [key, value] : res->headers) {
            response.headers[key] = value;
        }

        if (response.status_code >= 400) {
            return Error{
                "HTTP error " + std::to_string(response.status_code),
                base_url_ + full_path,
                response.status_code
            };
        }

        return response;
    });
}

Result<HttpResponse> HttpClient::post(const std::string& path,
                                      const std::string& body,
                                      const std::string& content_type) {
    Logger::debug("HTTP POST: {}{}", base_url_, path);

    return execute_with_retry([this, &path, &body, &content_type]() -> Result<HttpResponse> {
        httplib::Headers headers;
        headers.emplace("User-Agent", user_agent_);
        headers.emplace("Content-Type", content_type);
        for (const auto& [key, value] : custom_headers_) {
            headers.emplace(key, value);
        }

        auto res = client_->Post(path, headers, body, content_type);

        if (!res) {
            auto err = res.error();
            return Error{"HTTP request failed", base_url_ + path, static_cast<int>(err)};
        }

        HttpResponse response;
        response.status_code = res->status;
        response.body = res->body;
        for (const auto& [key, value] : res->headers) {
            response.headers[key] = value;
        }

        if (response.status_code >= 400) {
            return Error{
                "HTTP error " + std::to_string(response.status_code),
                base_url_ + path,
                response.status_code
            };
        }

        return response;
    });
}

Result<HttpResponse> HttpClient::execute_with_retry(
    std::function<Result<HttpResponse>()> request_fn) {

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        auto result = request_fn();

        if (result) {
            if (attempt > 0) {
                Logger::info("Request succeeded after {} retries", attempt);
            }
            return result;
        }

        const auto& error = result.error();
        bool should_retry = is_retryable_error(error.code);

        if (!should_retry || attempt == max_retries_) {
            Logger::error("Request failed: {}", error.format());
            return result;
        }

        auto delay = calculate_retry_delay(attempt);
        Logger::warn("Request failed (attempt {}/{}): {}. Retrying in {}ms...",
                    attempt + 1, max_retries_ + 1, error.message,
                    delay.count());

        std::this_thread::sleep_for(delay);
    }

    return Error{"Max retries exceeded"};
}

bool HttpClient::is_retryable_error(int status_code) const {
    return status_code < 0 || (status_code >= 500 && status_code < 600);
}

std::chrono::milliseconds HttpClient::calculate_retry_delay(int attempt) const {
    long delay_ms = static_cast<long>(initial_retry_delay_ms_) * (1L << attempt);
    if (delay_ms > 60000L) {
        delay_ms = 60000L;
    }
    return std::chrono::milliseconds(delay_ms);
}

std::string HttpClient::build_query_string(
    const std::map<std::string, std::string>& params) const {

    std::ostringstream oss;
    bool first = true;

    for (const auto& [key, value] : params) {
        if (!first) {
            oss << "&";
        }
        first = false;

        oss << url_encode(key) << "=" << url_encode(value);
    }

    return oss.str();
}

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}

} // namespace ibkr::utils
