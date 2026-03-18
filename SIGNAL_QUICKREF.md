# Signal Integration - Quick Reference

## Setup (One-Time)

```bash
# 1. Start Docker container
docker-compose up -d signal-api

# 2. Register phone number (use dedicated number, NOT personal)
curl -X POST http://localhost:8080/v1/register/+1234567890

# 3. Verify with SMS code
curl -X POST http://localhost:8080/v1/register/+1234567890/verify/123456

# 4. Test
curl -X POST -H "Content-Type: application/json" \
  -d '{"message": "Test", "number": "+1234567890", "recipients": ["+0987654321"]}' \
  http://localhost:8080/v2/send
```

## Configuration

Add to `~/.ibkr-options-analyzer/config.json`:

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

## Usage in C++

```cpp
#include "utils/signal_notifier.hpp"

// Create notifier
SignalNotifier notifier("localhost", 8080, "+1234567890");

// Check health
auto health = notifier.check_health();
if (!health) {
    Logger::error("Signal API not available: {}", health.error().message);
}

// Send simple message
auto result = notifier.send_message("+0987654321", "Alert message");

// Send risk alert
notifier.send_risk_alert(
    "+0987654321",
    "Main Account",
    "Portfolio Delta",
    1250.0,  // current value
    1000.0   // threshold
);

// Send to multiple recipients
notifier.send_message(
    {"+1111111111", "+2222222222"},
    "Broadcast message"
);
```

## Cron Jobs

```bash
# Hourly analysis with alerts
0 * * * * /path/to/ibkr-options-analyzer analyze --alert-signal

# Daily summary at 9 AM
0 9 * * * /path/to/ibkr-options-analyzer report --signal-summary
```

## Troubleshooting

### Container not running
```bash
docker-compose ps
docker-compose up -d signal-api
docker-compose logs signal-api
```

### API not responding
```bash
curl http://localhost:8080/v1/about
```

### Registration issues
- Use dedicated phone number (not your personal Signal)
- Check SMS/voice call for verification code
- Token is tied to IP address (may need to re-register if IP changes)

### Messages not received
- Recipient must accept contact request in Signal app
- Check rate limits (avoid >100 messages/hour)
- Verify phone numbers include country code (+1 for US)

## Files

- **SIGNAL_SETUP.md** - Detailed setup guide
- **SIGNAL_INTEGRATION.md** - Technical implementation details
- **docker-compose.yml** - Docker configuration
- **test_signal.sh** - Automated test script
- **examples/example_signal.cpp** - Standalone example

## Phone Number Options

**DO NOT use your personal Signal number!**

Options for dedicated number:
- Google Voice (free, US only)
- Twilio ($1-2/month)
- Burner phone with prepaid SIM

## Security Notes

- API runs on localhost only (not exposed to internet)
- Phone numbers stored in config.json (chmod 600)
- One-time registration (persists in Docker volume)
- For remote access, use SSH tunnel or VPN (Tailscale)
