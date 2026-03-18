# IBKR Options Analyzer

Modern C++20 command-line tool for tracking and analyzing non-expired open option positions from multiple Interactive Brokers accounts. Focused on option selling strategies (especially short puts) with comprehensive risk analysis.

## Features

- **Multi-account support**: Track positions across multiple IBKR accounts
- **Automated data download**: Via IBKR Flex Web Service API
- **Smart filtering**: Only non-expired options (expiry > current date)
- **Strategy detection**: Auto-detect naked short puts, spreads, covered calls
- **Risk analysis**: Breakeven, max profit/loss, distance to strike, portfolio totals
- **Real-time prices**: Fetch current stock prices from Yahoo Finance / Alpha Vantage
- **Signal notifications**: Send risk alerts via Signal messenger (optional)
- **Manual position entry**: Add custom/what-if positions for analysis
- **Export capabilities**: CSV export for Excel/Google Sheets

## Quick Start

### 1. Build the Project

```bash
# Configure (debug build)
cmake --preset debug

# Build
cmake --build build/debug

# Or for release build
cmake --preset release
cmake --build build/release
```

### 2. Set Up IBKR Flex Query

**CRITICAL**: You must create a Flex Query in IBKR Account Management before using this tool.

#### Step-by-Step Flex Query Setup:

1. Log in to [IBKR Account Management](https://www.interactivebrokers.com/portal)

2. Navigate to: **Reports** → **Flex Queries** → **Activity Flex Query**

3. Click **Create** and configure:

   **General Settings:**
   - Name: "Options Analyzer Query"
   - Format: CSV
   - Period: Last 365 Calendar Days (or your preference)
   - Date Format: yyyyMMdd
   - Time Format: HHMMSS
   - Include: Trades, Open Positions

   **Trades Section - Select these columns:**
   - Account
   - TradeDate
   - Symbol
   - Description
   - Underlying Symbol
   - Expiry
   - Strike
   - Put/Call
   - Quantity
   - TradePrice
   - Proceeds
   - Commission
   - NetCash
   - Asset Category (filter to: OPT)

   **Open Positions Section - Select these columns:**
   - Account
   - Symbol
   - Description
   - Underlying Symbol
   - Expiry
   - Strike
   - Put/Call
   - Quantity
   - MarkPrice
   - Position Value
   - Asset Category (filter to: OPT)

4. **Save** the query and note the **Query ID** (e.g., "123456")

5. Enable **Flex Web Service**:
   - Go to: **Settings** → **Account Settings** → **Flex Web Service**
   - Click **Generate Token**
   - **IMPORTANT**: Save this token securely (you can't retrieve it later)
   - Note: Token is tied to your IP address (may need to regenerate if IP changes)

### 3. Configure the Tool

```bash
# Run setup script
./setup.sh
```

Or manually:

```bash
# Create config directory
mkdir -p ~/.ibkr-options-analyzer/logs

# Copy minimal config
cp config.json.minimal ~/.ibkr-options-analyzer/config.json
```

**Minimal config.json:**
```json
{
  "database": {
    "path": "~/.ibkr-options-analyzer/data.db"
  },
  "logging": {
    "level": "info",
    "file": "~/.ibkr-options-analyzer/logs/app.log"
  }
}
```

**Note**: Account credentials (token and query_id) are provided via command-line arguments, not stored in the config file.

### 4. First Run

```bash
# Download data using command-line credentials
./build/release/ibkr-options-analyzer download \
  --token YOUR_FLEX_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Main Account"

# Import downloaded data into database
./build/release/ibkr-options-analyzer import

# Analyze open positions
./build/release/ibkr-options-analyzer analyze open

# View detected strategies
./build/release/ibkr-options-analyzer analyze strategy

# Generate full report
./build/release/ibkr-options-analyzer report --output report.csv
```

**Security Tip**: For automation, use environment variables:
```bash
export IBKR_TOKEN="your_token_here"
export IBKR_QUERY_ID="your_query_id"

./build/release/ibkr-options-analyzer download \
  --token "$IBKR_TOKEN" \
  --query-id "$IBKR_QUERY_ID" \
  --account "Main Account"
```

## Usage

### Download Command
```bash
# Download with command-line credentials
ibkr-options-analyzer download \
  --token YOUR_FLEX_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Main Account"

# Force re-download (skip cache)
ibkr-options-analyzer download \
  --token YOUR_FLEX_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Main Account" \
  --force

# Multiple accounts - run command multiple times
ibkr-options-analyzer download \
  --token TOKEN1 \
  --query-id QUERY1 \
  --account "Trading Account"

ibkr-options-analyzer download \
  --token TOKEN2 \
  --query-id QUERY2 \
  --account "IRA Account"
```

### Import Command
```bash
# Import all downloaded CSV files
ibkr-options-analyzer import

# Import specific file
ibkr-options-analyzer import --file /path/to/flex_report.csv
```

### Manual Add Command
```bash
# Add a manual position (what-if analysis)
ibkr-options-analyzer manual-add \
  --account "Main Account" \
  --underlying AAPL \
  --expiry 20250321 \
  --strike 150.0 \
  --right P \
  --quantity -1 \
  --premium 2.50

# Quantity: negative = short, positive = long
# Premium: per share (will be multiplied by 100 for contract value)
```

### Analyze Commands
```bash
# Show all open non-expired positions
ibkr-options-analyzer analyze open

# Show risk/impact analysis
ibkr-options-analyzer analyze impact

# Show detected strategies
ibkr-options-analyzer analyze strategy

# Filter by account
ibkr-options-analyzer analyze open --account "Main Account"

# Filter by underlying
ibkr-options-analyzer analyze open --underlying AAPL
```

### Report Command
```bash
# Generate text report to console
ibkr-options-analyzer report

# Export to CSV
ibkr-options-analyzer report --output report.csv

# Include only specific account
ibkr-options-analyzer report --account "IRA Account" --output ira_report.csv
```

## Understanding the Output

### Open Positions Table
```
Account      | Symbol              | Underlying | Expiry     | Strike | Right | Qty  | Mark   | Value
-------------|---------------------|------------|------------|--------|-------|------|--------|--------
Main Account | AAPL  250321P00150000| AAPL      | 2025-03-21 | 150.00 | P     | -1   | 2.35   | -235.00
```

### Risk Analysis
- **Breakeven**: Strike - premium collected (for short puts)
- **Max Profit**: Premium collected (for short options)
- **Max Loss**: Strike × 100 - premium (for naked short puts)
- **Distance to Strike**: (Current price - Strike) / Current price × 100%
- **Delta**: Approximate delta based on mark price (if available)

### Strategy Detection
- **Naked Short Put**: Single short put, no other legs
- **Short Put Spread**: Short put + long put (lower strike), same expiry
- **Covered Call**: Long stock + short call
- **Iron Condor**: Short put spread + short call spread

## Signal Notifications (Optional)

Send risk alerts via Signal messenger when thresholds are breached.

### Setup

1. **Start Signal API container:**
```bash
docker-compose up -d signal-api
```

2. **Register your phone number** (use a dedicated number, NOT your personal Signal):
```bash
curl -X POST http://localhost:8080/v1/register/+1234567890
```

3. **Verify with SMS code:**
```bash
curl -X POST http://localhost:8080/v1/register/+1234567890/verify/123456
```

4. **Update config.json:**
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
    "alert_on_breach": true
  }
}
```

5. **Test setup:**
```bash
./test_signal.sh
```

See [SIGNAL_SETUP.md](SIGNAL_SETUP.md) for detailed instructions.

### Usage with Cron

```bash
# Run analysis every hour and send alerts if thresholds breached
0 * * * * /path/to/ibkr-options-analyzer analyze --alert-signal

# Daily summary at 9 AM
0 9 * * * /path/to/ibkr-options-analyzer report --signal-summary
```

## Troubleshooting

### "Token expired or invalid"
- Flex tokens are tied to your IP address
- Regenerate token in IBKR Account Management if your IP changed
- Ensure token is correctly copied (no extra spaces)
- Use the new token with `--token` argument

### "Query ID not found"
- Verify Query ID in IBKR Account Management → Flex Queries
- Ensure query includes both Trades and Open Positions sections
- Query must be saved (not just created)
- Use the correct query ID with `--query-id` argument

### "No data returned"
- Check date range in Flex Query (must include recent trades)
- Verify Asset Category filter includes OPT (options)
- Ensure you have option positions in the account

### "Failed to parse option symbol"
- IBKR uses multiple symbol formats (e.g., "AAPL  250321C00150000" or "AAPL250321C150")
- Tool handles both formats automatically
- If parsing fails, check symbol format in CSV and report issue

### Build Issues
- Ensure CMake 3.26+ and Ninja are installed
- Ensure C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- All dependencies are fetched automatically via FetchContent

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed component design.

## Development

```bash
# Debug build with sanitizers
cmake --preset debug
cmake --build build/debug

# Run with verbose logging
./build/debug/ibkr-options-analyzer --log-level debug download

# Run tests (when implemented)
cmake --build build/debug --target test
```

## License

Personal use only. Not affiliated with Interactive Brokers.

## Disclaimer

This tool is for personal portfolio analysis only. It does not provide investment advice. Options trading involves significant risk. Always verify calculations independently before making trading decisions.
