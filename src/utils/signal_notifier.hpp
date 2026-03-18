#pragma once

#include "result.hpp"
#include "http_client.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ibkr::utils {

/**
 * Signal messenger notification client.
 *
 * Sends alerts via Signal using signal-cli-rest-api Docker container.
 * Requires signal-cli-rest-api running on localhost:8080 (configurable).
 *
 * Setup:
 *   1. Run: docker-compose up -d signal-api
 *   2. Register: curl -X POST http://localhost:8080/v1/register/+1234567890
 *   3. Verify: curl -X POST http://localhost:8080/v1/register/+1234567890/verify/CODE
 *
 * Usage:
 *   SignalNotifier notifier("localhost", 8080, "+1234567890");
 *   notifier.send_message("+0987654321", "Risk alert: Delta exceeded");
 */
class SignalNotifier {
public:
    /**
     * Constructor
     * @param api_host Signal API host (default: localhost)
     * @param api_port Signal API port (default: 8080)
     * @param from_number Registered Signal phone number (e.g., "+1234567890")
     */
    SignalNotifier(std::string api_host,
                   int api_port,
                   std::string from_number);

    /**
     * Send a simple text message
     * @param to_number Recipient phone number (e.g., "+0987654321")
     * @param message Message text
     * @return Result indicating success or error
     */
    Result<void> send_message(const std::string& to_number,
                              const std::string& message);

    /**
     * Send message to multiple recipients
     * @param to_numbers List of recipient phone numbers
     * @param message Message text
     * @return Result indicating success or error
     */
    Result<void> send_message(const std::vector<std::string>& to_numbers,
                              const std::string& message);

    /**
     * Send a formatted risk alert
     * @param to_number Recipient phone number
     * @param account Account name
     * @param metric_name Risk metric name (e.g., "Portfolio Delta")
     * @param current_value Current value
     * @param threshold Threshold value
     * @return Result indicating success or error
     */
    Result<void> send_risk_alert(const std::string& to_number,
                                 const std::string& account,
                                 const std::string& metric_name,
                                 double current_value,
                                 double threshold);

    /**
     * Check if Signal API is reachable
     * @return Result indicating if API is healthy
     */
    Result<void> check_health();

private:
    std::string api_host_;
    int api_port_;
    std::string from_number_;
    std::unique_ptr<HttpClient> http_client_;

    /**
     * Build base URL for Signal API
     */
    std::string build_base_url() const;
};

} // namespace ibkr::utils
