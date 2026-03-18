# Signal Notifications Integration - Summary

## What Was Added

Signal messaging integration for sending risk alerts from the IBKR Options Analyzer.

## Files Created

1. **src/utils/signal_notifier.hpp** - Signal notification client header
2. **src/utils/signal_notifier.cpp** - Signal notification implementation
3. **docker-compose.yml** - Docker setup for signal-cli-rest-api
4. **SIGNAL_SETUP.md** - Detailed setup and usage guide
5. **test_signal.sh** - Test script for Signal setup
6. **examples/example_signal.cpp** - Example C++ code
7. **config.json.signal-example** - Example config with Signal settings

## Files Modified

1. **CMakeLists.txt** - Added signal_notifier.cpp/hpp to build
2. **src/config/config_manager.hpp** - Added SignalConfig, NotificationConfig, RiskThresholdConfig structs
3. **src/config/config_manager.cpp** - Added parsing and validation for Signal config
4. **README.md** - Added Signal notifications section

## Architecture

```
┌─────────────────────────────────────┐
│  ibkr-options-analyzer (C++)        │
│  - Uses cpp-httplib (already in)   │
│  - SignalNotifier class             │
└──────────────┬──────────────────────┘
               │ HTTP POST
               ▼
┌─────────────────────────────────────┐
│  signal-cli-rest-api (Docker)       │
│  - Runs as daemon on localhost:8080 │
│  - Handles Signal protocol          │
└──────────────┬──────────────────────┘
               │ Signal Protocol
               ▼
┌─────────────────────────────────────┐
│  Signal Network                     │
│  - Delivers to recipient phones     │
└─────────────────────────────────────┘
```

## Key Features

- **Simple HTTP interface** - Uses existing cpp-httplib dependency
- **Docker-based** - No Java/Signal complexity in C++ code
- **Cron-friendly** - Daemon runs continuously, no startup overhead
- **Configurable** - Enable/disable via config.json
- **Risk alerts** - Formatted messages for threshold breaches

## Quick Start

1. **Start Docker container:**
```bash
docker-compose up -d signal-api
```

2. **Register phone number:**
```bash
curl -X POST http://localhost:8080/v1/register/+1234567890
curl -X POST http://localhost:8080/v1/register/+1234567890/verify/CODE
```

3. **Update config.json** with Signal settings

4. **Rebuild project:**
```bash
cmake --build build/release
```

5. **Test:**
```bash
./test_signal.sh
```

## Usage in Code

```cpp
#include "utils/signal_notifier.hpp"

// Create notifier
SignalNotifier notifier("localhost", 8080, "+1234567890");

// Send simple message
notifier.send_message("+0987654321", "Test alert");

// Send risk alert
notifier.send_risk_alert(
    "+0987654321",
    "Main Account",
    "Portfolio Delta",
    1250.0,  // current
    1000.0   // threshold
);
```

## Configuration Example

```json
{
  "notifications": {
    "signal": {
      "enabled": true,
      "api_host": "localhost",
      "api_port": 8080,
      "from_number": "+1234567890",
      "recipients": ["+0987654321"]
    }
  },
  "risk_thresholds": {
    "max_portfolio_delta": 1000.0,
    "max_position_size": 50000.0,
    "alert_on_breach": true
  }
}
```

## Important Notes

1. **Dedicated phone number required** - Do NOT use your personal Signal number (it will unlink your phone app)
2. **Options for phone numbers:**
   - Google Voice (free, US only)
   - Twilio ($1-2/month)
   - Burner phone with prepaid SIM

3. **Security:**
   - API runs on localhost only (not exposed to internet)
   - Phone numbers stored in config.json (chmod 600)
   - One-time registration (persists in Docker volume)

4. **Rate limits:**
   - Signal has undocumented rate limits
   - For cron jobs (hourly/daily), this is not a concern
   - Avoid sending >100 messages/hour

## Next Steps

To integrate Signal alerts into the analyze command:

1. Load Signal config from ConfigManager
2. Create SignalNotifier instance if enabled
3. Check risk thresholds after analysis
4. Send alerts if thresholds breached

Example integration point in `analyze_command.cpp`:

```cpp
// After risk calculation
if (config.notifications.signal.enabled &&
    config.risk_thresholds.alert_on_breach) {

    if (portfolio_delta > config.risk_thresholds.max_portfolio_delta) {
        SignalNotifier notifier(
            config.notifications.signal.api_host,
            config.notifications.signal.api_port,
            config.notifications.signal.from_number
        );

        for (const auto& recipient : config.notifications.signal.recipients) {
            notifier.send_risk_alert(
                recipient,
                account_name,
                "Portfolio Delta",
                portfolio_delta,
                config.risk_thresholds.max_portfolio_delta
            );
        }
    }
}
```

## Documentation

- **SIGNAL_SETUP.md** - Complete setup guide with troubleshooting
- **README.md** - Quick start section added
- **examples/example_signal.cpp** - Standalone example
- **test_signal.sh** - Automated setup verification

## Build Status

✅ CMakeLists.txt updated
✅ Config structs added
✅ Config parsing implemented
✅ Signal notifier implemented
✅ Docker compose configured
✅ Documentation complete

Ready to build and test!
