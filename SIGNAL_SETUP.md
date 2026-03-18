# Signal Notifications Setup Guide

This guide explains how to set up Signal notifications for risk alerts in ibkr-options-analyzer.

## Overview

The tool uses **signal-cli-rest-api** (Docker container) to send Signal messages. This provides a simple HTTP interface for sending alerts without requiring Java or complex Signal protocol implementation.

## Prerequisites

1. **Docker** installed and running
2. **Dedicated phone number** for Signal (DO NOT use your personal Signal number)
   - Options: Google Voice (free, US only), Twilio ($1-2/month), burner phone
   - Using your personal number will unlink your Signal app!

## Setup Steps

### 1. Start the Signal API Container

```bash
# From project root
docker-compose up -d signal-api

# Check logs
docker-compose logs -f signal-api
```

The API will be available at `http://localhost:8080`

### 2. Register Your Phone Number

Replace `+1234567890` with your dedicated phone number:

```bash
# Request verification code (sent via SMS)
curl -X POST http://localhost:8080/v1/register/+1234567890

# If SMS doesn't work, try voice call
curl -X POST "http://localhost:8080/v1/register/+1234567890?use_voice=true"
```

You should receive a 6-digit verification code via SMS or voice call.

### 3. Verify the Code

Replace `123456` with the code you received:

```bash
curl -X POST http://localhost:8080/v1/register/+1234567890/verify/123456
```

If successful, you'll see:
```json
{"status": "verified"}
```

### 4. Test Sending a Message

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "message": "Test message from IBKR Options Analyzer",
    "number": "+1234567890",
    "recipients": ["+0987654321"]
  }' \
  http://localhost:8080/v2/send
```

Replace:
- `+1234567890` = your registered number (sender)
- `+0987654321` = your personal phone number (recipient)

### 5. Update config.json

Add Signal configuration to your `~/.ibkr-options-analyzer/config.json`:

```json
{
  "accounts": [
    // ... existing accounts ...
  ],
  "notifications": {
    "signal": {
      "enabled": true,
      "api_host": "localhost",
      "api_port": 8080,
      "from_number": "+1234567890",
      "recipients": [
        "+0987654321"
      ]
    }
  },
  "risk_thresholds": {
    "max_portfolio_delta": 1000.0,
    "max_position_size": 50000.0,
    "alert_on_breach": true
  }
}
```

### 6. Test from C++ Code

```bash
# Build the tool
cmake --build build/release

# Run analysis with Signal alerts enabled
./build/release/ibkr-options-analyzer analyze --alert-signal
```

## Usage Examples

### Send Manual Alert

```cpp
#include "utils/signal_notifier.hpp"

// Create notifier
SignalNotifier notifier("localhost", 8080, "+1234567890");

// Send simple message
auto result = notifier.send_message("+0987654321", "Test alert");
if (!result) {
    Logger::error("Failed to send: {}", result.error().message);
}

// Send risk alert
notifier.send_risk_alert(
    "+0987654321",
    "Main Account",
    "Portfolio Delta",
    1250.0,  // current value
    1000.0   // threshold
);
```

### Cron Job Integration

Add to your crontab (`crontab -e`):

```bash
# Run analysis every hour and send alerts if thresholds breached
0 * * * * /path/to/ibkr-options-analyzer analyze --alert-signal >> /var/log/ibkr-alerts.log 2>&1

# Daily summary at 9 AM
0 9 * * * /path/to/ibkr-options-analyzer report --format text --signal-summary
```

## Troubleshooting

### "Connection refused" Error

**Problem:** Docker container not running

**Solution:**
```bash
docker-compose up -d signal-api
docker-compose ps  # Check status
```

### "Authentication failed" Error

**Problem:** Phone number not registered or verification expired

**Solution:** Re-register following steps 2-3 above

### "Rate limit exceeded" Error

**Problem:** Sending too many messages too quickly

**Solution:** Signal has undocumented rate limits. For cron jobs (hourly/daily), this shouldn't be an issue. Avoid sending >100 messages/hour.

### Messages Not Received

**Problem:** Recipient hasn't accepted your contact request

**Solution:**
1. Send a test message from your registered number
2. Recipient must accept the contact request in their Signal app
3. Retry sending

### Docker Container Keeps Restarting

**Problem:** Configuration issue or port conflict

**Solution:**
```bash
# Check logs
docker-compose logs signal-api

# Check if port 8080 is in use
lsof -i :8080

# Use different port in docker-compose.yml
ports:
  - "8081:8080"  # Map to 8081 instead
```

## Security Considerations

1. **Localhost Only:** The Docker container binds to localhost by default. Do NOT expose port 8080 to the internet.

2. **Phone Number Storage:** Store phone numbers in `config.json` which should have restricted permissions:
   ```bash
   chmod 600 ~/.ibkr-options-analyzer/config.json
   ```

3. **Remote Access:** If running on a remote server, use SSH tunnel or VPN (Tailscale):
   ```bash
   # SSH tunnel
   ssh -L 8080:localhost:8080 user@remote-server
   ```

4. **Dedicated Number:** Never use your personal Signal number. Registration will unlink your phone app.

## Maintenance

### Update Container

```bash
docker-compose pull signal-api
docker-compose up -d signal-api
```

### Backup Registration Data

```bash
# Registration data is stored in volume
tar -czf signal-cli-backup.tar.gz ~/.ibkr-options-analyzer/signal-cli/
```

### Unregister Number

```bash
curl -X POST http://localhost:8080/v1/unregister/+1234567890
```

## API Reference

### Health Check
```bash
curl http://localhost:8080/v1/about
```

### Send Message
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"message": "text", "number": "+sender", "recipients": ["+recipient"]}' \
  http://localhost:8080/v2/send
```

### Receive Messages
```bash
curl http://localhost:8080/v1/receive/+1234567890
```

## Resources

- [signal-cli-rest-api Documentation](https://github.com/bbernhard/signal-cli-rest-api)
- [signal-cli Wiki](https://github.com/AsamK/signal-cli/wiki)
- [API Examples](https://github.com/bbernhard/signal-cli-rest-api/blob/master/doc/EXAMPLES.md)
