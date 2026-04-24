# IBKR Options Analyzer - User Guide

A command-line tool for tracking and analyzing option positions from Interactive Brokers accounts.

## Quick Start

### Installation

1. **Build the tool:**
   ```bash
   cmake --preset release
   cmake --build build/release
   ```

2. **Set up minimal configuration:**
   ```bash
   mkdir -p ~/.ibkr-options-analyzer
   cp config.json.minimal ~/.ibkr-options-analyzer/config.json
   ```

   The minimal config only requires database and logging settings. Account credentials are provided via command-line arguments.

### Getting IBKR Flex Tokens

1. Log in to [IBKR Account Management](https://www.interactivebrokers.com/portal)
2. Navigate to **Reports → Flex Queries**
3. Create a new Activity Flex Query with these settings:
   - Include: Open Positions
   - Format: CSV
   - Date Range: Today
4. Generate a token for the query (note: tokens are IP-restricted)
5. Copy the token and query ID to your config.json

## Configuration

The tool uses a minimal config file at `~/.ibkr-options-analyzer/config.json`:

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

**Account credentials are provided via command-line arguments** (not stored in config file).

### Optional Advanced Configuration

You can optionally configure HTTP and Flex settings:

```json
{
  "database": {
    "path": "~/.ibkr-options-analyzer/data.db"
  },
  "http": {
    "user_agent": "IBKROptionsAnalyzer/1.0",
    "timeout_seconds": 30,
    "max_retries": 5,
    "retry_delay_ms": 2000
  },
  "flex": {
    "poll_interval_seconds": 5,
    "max_poll_duration_seconds": 300
  },
  "logging": {
    "level": "info",
    "file": "~/.ibkr-options-analyzer/logs/app.log",
    "max_file_size_mb": 10,
    "max_files": 5
  }
}
```

### Legacy: Storing Accounts in Config

If you prefer to store account credentials in the config file (not recommended for security), you can add an `accounts` array. See `config.json.example` for the format.

## Usage

### Download Positions

Fetch current positions from IBKR using command-line credentials:

```bash
./build/release/ibkr-options-analyzer download \
  --token YOUR_FLEX_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Main Account"
```

**Required arguments:**
- `--token`: IBKR Flex Web Service token
- `--query-id`: IBKR Flex query ID
- `--account`: Friendly name for this account

**Optional arguments:**
- `--force`: Force re-download (skip cache)

This will:
1. Request reports from IBKR Flex Web Service
2. Parse option positions
3. Store them in the local SQLite database

### View Positions

List all open option positions grouped by duration:

```bash
./build/release/ibkr-options-analyzer analyze open
```

Filter by account or underlying:

```bash
./build/release/ibkr-options-analyzer analyze open --account "Main Account"
./build/release/ibkr-options-analyzer analyze open --underlying AAPL
```

### Analyze Strategies

Detect option strategies and calculate risk metrics:

```bash
# Show all positions with risk levels
./build/release/ibkr-options-analyzer analyze open

# Impact analysis for a specific underlying
./build/release/ibkr-options-analyzer analyze impact --underlying AAPL

# Detected strategies with risk metrics
./build/release/ibkr-options-analyzer analyze strategy
```

Supported strategies:
- Naked Short Put / Naked Short Call
- Bull Put Spread
- Bear Call Spread
- Iron Condor

### JSON Output

All commands support `--format json` for structured output (Python/dashboard integration):

```bash
# JSON output
./build/release/ibkr-options-analyzer --format json analyze open 2>/dev/null

# Pipe to Python
./build/release/ibkr-options-analyzer --format json --quiet analyze strategy 2>/dev/null | python3 -m json.tool

# JSON report
./build/release/ibkr-options-analyzer --format json report 2>/dev/null
```

In JSON mode, log messages go to stderr so stdout contains only clean JSON.

### Get Help

```bash
./build/release/ibkr-options-analyzer --help
./build/release/ibkr-options-analyzer download --help
```

## Common Options

All commands support:
- `--config PATH`: Use custom config file
- `--log-level LEVEL`: Set logging verbosity (`trace`, `debug`, `info`, `warn`, `error`)
- `--format json`: Output structured JSON (for Python integration)
- `--quiet`: Suppress human-readable output (combine with `--format json`)
- `--help`: Show command help

## Example Workflow

```bash
# 1. Download positions from your account
./build/release/ibkr-options-analyzer download \
  --token "123456789abcdef" \
  --query-id "654321" \
  --account "Trading Account"

# 2. Import the downloaded CSV into database
./build/release/ibkr-options-analyzer import

# 3. Analyze your positions
./build/release/ibkr-options-analyzer analyze open

# 4. Generate a report
./build/release/ibkr-options-analyzer report --output positions.csv
```

## Troubleshooting

### Token Expired or Invalid

**Error:** `HTTP 401` or "Invalid token"

**Solution:**
1. Log in to IBKR Account Management
2. Regenerate your Flex token
3. Use the new token with `--token` argument

### IP Restriction Error

**Error:** `HTTP 403` or "IP not allowed"

**Solution:**
- IBKR tokens are tied to your IP address
- Regenerate the token if your IP changed
- Ensure you're not using a VPN that changes IPs

### Query Not Found

**Error:** `HTTP 404` or "Query not found"

**Solution:**
- Verify the query ID with `--query-id` argument
- Check that the query exists in IBKR Account Management
- Ensure the query is set to "Activity" type (not "Trade Confirmation")

### No Positions Found

**Possible causes:**
- No open option positions in the account
- Query date range doesn't include today
- Query filters exclude options

**Solution:**
- Check your IBKR Flex Query settings
- Ensure "Open Positions" is included
- Run with `--log-level debug` to see detailed parsing info

### Build Errors

**Missing CMake or Ninja:**
```bash
# macOS
brew install cmake ninja

# Ubuntu/Debian
sudo apt install cmake ninja-build

# Fedora
sudo dnf install cmake ninja-build
```

**Compiler too old:**
- Requires C++20 support (GCC 10+, Clang 12+, MSVC 2019+)

## File Locations

- **Config:** `~/.ibkr-options-analyzer/config.json`
- **Database:** `~/.ibkr-options-analyzer/data.db`
- **Logs:** `~/.ibkr-options-analyzer/logs/app.log`

## Security Notes

- Tokens are provided via command line (not stored in config)
- For automation scripts, consider using environment variables
- IBKR tokens are tied to IP address (may need regeneration)
- Never commit tokens to version control

## Advanced Usage

### Custom Database Location

Use a custom config file:

```bash
./build/release/ibkr-options-analyzer --config custom-config.json download \
  --token YOUR_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Account Name"
```

### Debug Mode

Enable verbose logging to troubleshoot issues:

```bash
./build/release/ibkr-options-analyzer --log-level debug download \
  --token YOUR_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Account Name"
```

Check the log file:
```bash
tail -f ~/.ibkr-options-analyzer/logs/app.log
```

### Multiple Accounts

Download from multiple accounts by running the command multiple times:

```bash
# Account 1
./build/release/ibkr-options-analyzer download \
  --token TOKEN1 \
  --query-id QUERY1 \
  --account "Trading Account"

# Account 2
./build/release/ibkr-options-analyzer download \
  --token TOKEN2 \
  --query-id QUERY2 \
  --account "IRA Account"
```

All positions are stored in the same database and can be analyzed together.

### Using Environment Variables

For automation, use environment variables:

```bash
export IBKR_TOKEN="your_token_here"
export IBKR_QUERY_ID="your_query_id"

./build/release/ibkr-options-analyzer download \
  --token "$IBKR_TOKEN" \
  --query-id "$IBKR_QUERY_ID" \
  --account "Main Account"
```

## Support

For issues or questions:
- Check the log file: `~/.ibkr-options-analyzer/logs/app.log`
- Review IBKR Flex Web Service documentation
- Verify your Flex Query configuration in IBKR portal

## Limitations

- Only tracks option positions (no stocks, futures, forex)
- Requires IBKR Flex Web Service access
- Yahoo Finance prices may be unavailable for some international symbols
- Greeks calculation not yet implemented
