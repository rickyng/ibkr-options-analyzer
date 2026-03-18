// Example: Send Signal notification from C++
// Compile: g++ -std=c++20 -I../src example_signal.cpp ../src/utils/signal_notifier.cpp ../src/utils/http_client.cpp ../src/utils/logger.cpp -lfmt -lspdlog -o example_signal

#include "utils/signal_notifier.hpp"
#include "utils/logger.hpp"
#include <iostream>

int main() {
    // Initialize logger
    ibkr::utils::Logger::init("info", "");

    // Create Signal notifier
    // Make sure signal-cli-rest-api is running: docker-compose up -d signal-api
    ibkr::utils::SignalNotifier notifier(
        "localhost",
        8080,
        "+1234567890"  // Your registered Signal number
    );

    // Check health
    std::cout << "Checking Signal API health...\n";
    auto health = notifier.check_health();
    if (!health) {
        std::cerr << "Error: " << health.error().format() << "\n";
        std::cerr << "Make sure Docker container is running: docker-compose up -d signal-api\n";
        return 1;
    }
    std::cout << "✓ Signal API is healthy\n\n";

    // Send simple message
    std::cout << "Sending test message...\n";
    auto result = notifier.send_message(
        "+0987654321",  // Recipient number
        "Hello from IBKR Options Analyzer! This is a test message."
    );

    if (!result) {
        std::cerr << "Error: " << result.error().format() << "\n";
        return 1;
    }
    std::cout << "✓ Message sent successfully\n\n";

    // Send risk alert
    std::cout << "Sending risk alert...\n";
    auto alert_result = notifier.send_risk_alert(
        "+0987654321",
        "Main Account",
        "Portfolio Delta",
        1250.0,  // Current value
        1000.0   // Threshold
    );

    if (!alert_result) {
        std::cerr << "Error: " << alert_result.error().format() << "\n";
        return 1;
    }
    std::cout << "✓ Risk alert sent successfully\n";

    return 0;
}
