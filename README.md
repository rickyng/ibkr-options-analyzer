# IBKR Options Analyzer

Modern C++20 command-line tool for tracking and analyzing non-expired open option positions from multiple Interactive Brokers accounts. Focused on option selling strategies (especially short puts) with comprehensive risk analysis.

## Features

- **Multi-account support**: Track positions across multiple IBKR accounts
- **Automated data download**: Via IBKR Flex Web Service API
- **Smart filtering**: Only non-expired options (expiry > current date)
- **Strategy detection**: Auto-detect naked short puts/calls, bull put spreads, bear call spreads, iron condors
- **Risk analysis**: Breakeven, max profit/loss, assignment risk levels, portfolio totals
- **Portfolio review**: Consolidated view with assignment risk alerts, P&L, expiration calendar
- **Opportunity screener**: Watchlist-based put-selling scanner with grading (A-D) and parameter overrides
- **Price caching**: SQLite-backed cache for prices, volatility, and option chains with trading-day TTL
- **Real-time prices**: Yahoo Finance integration for current price and distance-to-strike
- **Duration buckets**: Positions grouped by expiry (1w, 2w, 3w, 3w+)
- **Expiry timeline table**: Stock-level view of strikes across DTE buckets (<7d, 7-14d, 14-21d, 21-28d, >28d)
- **JSON output**: `--format json` flag for Python/dashboard integration
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

# Portfolio review with risk alerts
./build/release/ibkr-options-analyzer analyze portfolio

# Screener with parameter overrides
./build/release/ibkr-options-analyzer analyze screener --min-iv-percentile 40 --max-dte 30

# Screener with cached data only (no API calls)
./build/release/ibkr-options-analyzer analyze screener --cache-only

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
# Show all open non-expired positions (grouped by duration)
ibkr-options-analyzer analyze open

# Show impact analysis for a single underlying
ibkr-options-analyzer analyze impact --underlying AAPL

# Show detected strategies with risk metrics
ibkr-options-analyzer analyze strategy

# Filter by account or underlying
ibkr-options-analyzer analyze open --account "Main Account"
ibkr-options-analyzer analyze open --underlying AAPL
```

### JSON Output (for Python integration)
```bash
# JSON output for all commands
ibkr-options-analyzer --format json analyze open 2>/dev/null
ibkr-options-analyzer --format json analyze strategy 2>/dev/null
ibkr-options-analyzer --format json report 2>/dev/null

# Suppress all human-readable output (pipe-friendly)
ibkr-options-analyzer --format json --quiet analyze open 2>/dev/null | python3 -m json.tool
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

### Open Positions (Duration Buckets)
Positions are grouped by time to expiry:
- **≤1 week**: Expiring within 7 days
- **≤2 weeks**: 8-14 days to expiry
- **≤3 weeks**: 15-21 days to expiry
- **>3 weeks**: More than 21 days

Each position shows: underlying, expiry, strike, right, quantity, entry premium, current price (if available), and distance to strike with risk indicator (ITM/near/OTM).

### Risk Analysis
- **Breakeven**: Strike - premium collected (for short puts)
- **Max Profit**: Premium collected (for short options)
- **Max Loss**: Strike × 100 - premium (for naked short puts); unlimited for short calls
- **Assignment Risk Levels**: CRITICAL (ITM/≤1% OTM), HIGH (1-5%), MODERATE (5-10%), SAFE (>10%)

### Strategy Detection
- **Naked Short Put/Call**: Single short option
- **Bull Put Spread**: Short put + long put (lower strike), same expiry
- **Bear Call Spread**: Short call + long call (higher strike), same expiry
- **Iron Condor**: Short put spread + short call spread

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

```
C++ Engine (src/)              Python Dashboard (dashboard/)
├── CLI commands               ├── FastAPI REST API (/api/*)
├── Services layer             ├── Dash frontend (Plotly + Bootstrap)
├── Core analysis              ├── Direct service calls (DB + CLI subprocess)
└── SQLite DB (read/write)     └── Dark theme UI with charts & tables
         ↕ shared SQLite
```

- **src/core/**: Static library (`ibkr_core`) with config, flex, parsers, db, analysis, report, utils, services
- **src/cli/**: CLI executable with command handlers (thin wrappers)
- **dashboard/**: Python FastAPI + Dash for web API and interactive dashboard
- **JSON output**: All commands support `--format json` for Python integration

## Docker

Run the dashboard in a container for easy deployment:

```bash
# Build the image (first time takes ~5-10 min for C++ deps)
docker compose build

# Start the dashboard
docker compose up -d

# View logs
docker compose logs -f

# Stop
docker compose down

# Rebuild after code changes
docker compose up -d --build
```

The SQLite database is persisted via volume mount at `~/.ibkr-options-analyzer/`.

Access the dashboard at http://localhost:8001 and the Swagger docs at http://localhost:8001/docs.

## Development

### C++ Engine
```bash
# Debug build with sanitizers
cmake --preset debug
cmake --build build/debug

# Run with verbose logging
./build/debug/ibkr-options-analyzer --log-level debug download

# Run tests (when implemented)
cmake --build build/debug --target test
```

### Python Dashboard
```bash
cd dashboard
uv sync                              # Install core dependencies
uv sync --extra dashboard            # Install Dash + Plotly
uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8001  # Start
uv run pytest                        # Run tests
```

## License

Personal use only. Not affiliated with Interactive Brokers.

## Disclaimer

This tool is for personal portfolio analysis only. It does not provide investment advice. Options trading involves significant risk. Always verify calculations independently before making trading decisions.
