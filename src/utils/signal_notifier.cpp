#include "signal_notifier.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fmt/format.h>

namespace ibkr::utils {

SignalNotifier::SignalNotifier(std::string api_host,
                               int api_port,
                               std::string from_number)
    : api_host_(std::move(api_host))
    , api_port_(api_port)
    , from_number_(std::move(from_number)) {

    // Create HTTP client for Signal API
    http_client_ = std::make_unique<HttpClient>(
        build_base_url(),
        "IBKROptionsAnalyzer/1.0",
        10,  // 10 second timeout
        2,   // 2 retries
        1000 // 1 second retry delay
    );
}

Result<void> SignalNotifier::send_message(const std::string& to_number,
                                          const std::string& message) {
    return send_message(std::vector<std::string>{to_number}, message);
}

Result<void> SignalNotifier::send_message(const std::vector<std::string>& to_numbers,
                                          const std::string& message) {
    Logger::debug("Sending Signal message to {} recipients", to_numbers.size());

    // Build JSON payload
    nlohmann::json payload = {
        {"message", message},
        {"number", from_number_},
        {"recipients", to_numbers}
    };

    // Send POST request to /v2/send
    auto response = http_client_->post("/v2/send",
                                       payload.dump(),
                                       "application/json");

    if (!response) {
        return Error{
            "Failed to send Signal message",
            response.error().message,
            response.error().code
        };
    }

    // Check status code (201 = success)
    if (response->status_code != 201) {
        // Try to parse error message from response
        std::string error_detail = "HTTP " + std::to_string(response->status_code);
        try {
            auto error_json = nlohmann::json::parse(response->body);
            if (error_json.contains("error")) {
                error_detail = error_json["error"].get<std::string>();
            }
        } catch (...) {
            // Ignore JSON parse errors
        }

        return Error{
            "Signal API returned error",
            error_detail,
            response->status_code
        };
    }

    Logger::info("Signal message sent successfully to {} recipients", to_numbers.size());
    return {};
}

Result<void> SignalNotifier::send_risk_alert(const std::string& to_number,
                                             const std::string& account,
                                             const std::string& metric_name,
                                             double current_value,
                                             double threshold) {
    // Format alert message
    std::string message = fmt::format(
        "🚨 RISK ALERT\n"
        "\n"
        "Account: {}\n"
        "Metric: {}\n"
        "Current: {:.2f}\n"
        "Threshold: {:.2f}\n"
        "\n"
        "Action required: Review positions",
        account,
        metric_name,
        current_value,
        threshold
    );

    return send_message(to_number, message);
}

Result<void> SignalNotifier::check_health() {
    Logger::debug("Checking Signal API health");

    // Try to GET /v1/health or /v1/about endpoint
    auto response = http_client_->get("/v1/about");

    if (!response) {
        return Error{
            "Signal API is not reachable",
            response.error().message,
            response.error().code
        };
    }

    if (response->status_code != 200) {
        return Error{
            "Signal API health check failed",
            "HTTP " + std::to_string(response->status_code),
            response->status_code
        };
    }

    Logger::debug("Signal API is healthy");
    return {};
}

std::string SignalNotifier::build_base_url() const {
    return fmt::format("http://{}:{}", api_host_, api_port_);
}

} // namespace ibkr::utils
