# IBKR Options Analyzer - Quick Reference

## Setup (One-time)

```bash
# Build
cmake --preset release && cmake --build build/release

# Setup config
./setup.sh
```

## Daily Usage

### Download Positions
```bash
./build/release/ibkr-options-analyzer download \
  --token YOUR_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Account Name"
```

### Import & Analyze
```bash
# Import to database
./build/release/ibkr-options-analyzer import

# View positions
./build/release/ibkr-options-analyzer analyze open

# View strategies
./build/release/ibkr-options-analyzer analyze strategy

# Generate report
./build/release/ibkr-options-analyzer report --output report.csv
```

## Command Reference

### download - Download Flex reports from IBKR
```bash
ibkr-options-analyzer download [OPTIONS]
```

**Required Options:**
- `--token TEXT` - IBKR Flex Web Service token
- `--query-id TEXT` - IBKR Flex query ID
- `--account TEXT` - Account name for this download

**Optional:**
- `--force` - Force re-download (skip cache)

---

### import - Import downloaded CSV files into database
```bash
ibkr-options-analyzer import [OPTIONS]
```

**Optional:**
- `--file TEXT` - Import specific CSV file (default: auto-detect all)

---

### manual-add - Manually add a position
```bash
ibkr-options-analyzer manual-add [OPTIONS]
```

**Required Options:**
- `--account TEXT` - Account name
- `--underlying TEXT` - Underlying symbol (e.g., AAPL)
- `--expiry TEXT` - Expiry date (YYYYMMDD format)
- `--strike FLOAT` - Strike price
- `--right TEXT` - Option right (C for call, P for put)
- `--quantity FLOAT` - Quantity (negative=short, positive=long)
- `--premium FLOAT` - Entry premium per share

**Optional:**
- `--notes TEXT` - Optional notes

---

### analyze - Analyze positions
```bash
ibkr-options-analyzer analyze TYPE [OPTIONS]
```

**Required:**
- `TYPE` - Analysis type: `open`, `impact`, or `strategy`

**Optional:**
- `--account TEXT` - Filter by account name
- `--underlying TEXT` - Filter by underlying symbol

**Analysis Types:**
- `open` - Show all open non-expired positions grouped by duration (≤1w, ≤2w, ≤3w, >3w) with:
  - Real-time stock prices (fetched from Yahoo Finance)
  - Assignment risk levels (CRITICAL, HIGH, MODERATE, SAFE)
  - Consolidated portfolio summary and capital requirements
- `impact` - Show risk/impact analysis for specific underlying
- `strategy` - Show detected strategies (spreads, naked positions, etc.)

**Assignment Risk Indicators:**
- ⚠️ ITM - Position is In The Money (at risk of assignment)
- ⚡ Near strike - Stock price within 5% of strike (watch closely)

---

### report - Generate comprehensive report
```bash
ibkr-options-analyzer report [OPTIONS]
```

**Optional:**
- `--output TEXT` - Output CSV file path
- `--type TEXT` - Report type: `full`, `positions`, `strategies`, `summary` (default: full)
- `--account TEXT` - Filter by account name
- `--underlying TEXT` - Filter by underlying symbol

---

## Global Options

Available for all commands:

| Option | Description |
|--------|-------------|
| `--config PATH` | Use custom config file (default: ~/.ibkr-options-analyzer/config.json) |
| `--log-level LEVEL` | Set logging level: trace, debug, info, warn, error (default: info) |
| `--format FORMAT` | Output format: text, json (default: text) |
| `--quiet` | Suppress human-readable output (use with --format json) |
| `--help` or `-h` | Show help message |

### JSON Output Examples
```bash
# Get positions as JSON
./build/release/ibkr-options-analyzer --format json analyze open 2>/dev/null

# Pipe to Python for processing
./build/release/ibkr-options-analyzer --format json --quiet analyze strategy 2>/dev/null | python3 -m json.tool
```

## Quick Examples

### Basic Workflow
```bash
# 1. Download positions
./build/release/ibkr-options-analyzer download \
  --token "YOUR_TOKEN" \
  --query-id "123456" \
  --account "Main Account"

# 2. Import to database
./build/release/ibkr-options-analyzer import

# 3. View open positions
./build/release/ibkr-options-analyzer analyze open

# 4. Detect strategies
./build/release/ibkr-options-analyzer analyze strategy

# 5. Generate CSV report
./build/release/ibkr-options-analyzer report --output report.csv
```

### Filter by Underlying
```bash
# Show only AAPL positions
./build/release/ibkr-options-analyzer analyze open --underlying AAPL

# Show only TSLA strategies
./build/release/ibkr-options-analyzer analyze strategy --underlying TSLA
```

### Manual Position Entry
```bash
# Add a short put position
./build/release/ibkr-options-analyzer manual-add \
  --account "Main Account" \
  --underlying AAPL \
  --expiry 20260417 \
  --strike 150.0 \
  --right P \
  --quantity -1 \
  --premium 2.50 \
  --notes "What-if analysis"
```

### Multiple Accounts
```bash
# Download from multiple accounts
./build/release/ibkr-options-analyzer download \
  --token TOKEN1 --query-id QUERY1 --account "Trading"

./build/release/ibkr-options-analyzer download \
  --token TOKEN2 --query-id QUERY2 --account "IRA"

# Import all
./build/release/ibkr-options-analyzer import

# Analyze all accounts together
./build/release/ibkr-options-analyzer analyze open

# Or filter by specific account
./build/release/ibkr-options-analyzer analyze open --account "Trading"
```

## Environment Variables (Optional)

Use environment variables for automation or to avoid typing credentials:

```bash
# Set environment variables
export IBKR_TOKEN="your_token"
export IBKR_QUERY_ID="your_query_id"
export ACCOUNT_NAME="Main Account"

# Use in commands
./build/release/ibkr-options-analyzer download \
  --token "$IBKR_TOKEN" \
  --query-id "$IBKR_QUERY_ID" \
  --account "$ACCOUNT_NAME"
```

## File Locations

- **Config:** `~/.ibkr-options-analyzer/config.json` - Database and logging settings
- **Database:** `~/.ibkr-options-analyzer/data.db` - SQLite database with positions
- **Logs:** `~/.ibkr-options-analyzer/logs/app.log` - Application logs
- **Downloads:** `~/.ibkr-options-analyzer/downloads/` - Downloaded CSV files

## Troubleshooting

### Enable Debug Logging
```bash
./build/release/ibkr-options-analyzer --log-level debug download \
  --token TOKEN --query-id QUERY --account "Account"
```

### View Logs
```bash
# Follow logs in real-time
tail -f ~/.ibkr-options-analyzer/logs/app.log

# View last 50 lines
tail -n 50 ~/.ibkr-options-analyzer/logs/app.log

# Search for errors
grep ERROR ~/.ibkr-options-analyzer/logs/app.log
```

### Force Re-download
```bash
# Skip cache and force fresh download
./build/release/ibkr-options-analyzer download --force \
  --token TOKEN --query-id QUERY --account "Account"
```

### Common Issues

**Token expired (HTTP 401):**
- Regenerate token in IBKR portal
- Tokens are tied to your IP address

**Query not found (HTTP 404):**
- Verify query ID in IBKR Flex Queries
- Ensure query is saved (not just created)

**No positions imported:**
- Check that query includes "Open Positions" section
- Verify Asset Category filter includes OPT (options)
- Run with `--log-level debug` to see parsing details

## Getting IBKR Credentials

### Step-by-Step Guide

1. **Login to IBKR Portal**
   - Go to: https://www.interactivebrokers.com/portal
   - Login with your credentials

2. **Create Flex Query**
   - Navigate to: **Reports → Flex Queries → Activity Flex Query**
   - Click **Create**
   - Configure:
     - Name: "Options Analyzer Query"
     - Format: CSV
     - Period: Last 365 Calendar Days
     - Include: **Open Positions** (required)
   - In Open Positions section, select columns:
     - Account, Symbol, Description, Underlying Symbol
     - Expiry, Strike, Put/Call, Quantity
     - MarkPrice, Position Value
   - Add filter: Asset Category = OPT
   - **Save** the query

3. **Get Query ID**
   - After saving, note the **Query ID** (e.g., "123456")
   - You'll use this with `--query-id`

4. **Generate Token**
   - Go to: **Settings → Account Settings → Flex Web Service**
   - Click **Generate Token**
   - **Important:** Save this token immediately (can't retrieve later)
   - Token is tied to your IP address
   - You'll use this with `--token`

5. **Test Your Setup**
   ```bash
   ./build/release/ibkr-options-analyzer download \
     --token "YOUR_TOKEN" \
     --query-id "YOUR_QUERY_ID" \
     --account "Main Account"
   ```

## Multiple Accounts

Run download command multiple times with different credentials:

```bash
# Account 1 - Trading
./build/release/ibkr-options-analyzer download \
  --token "TOKEN1" \
  --query-id "QUERY1" \
  --account "Trading"

# Account 2 - IRA
./build/release/ibkr-options-analyzer download \
  --token "TOKEN2" \
  --query-id "QUERY2" \
  --account "IRA"

# Import all downloaded files
./build/release/ibkr-options-analyzer import

# Analyze all accounts together
./build/release/ibkr-options-analyzer analyze open

# Or analyze specific account
./build/release/ibkr-options-analyzer analyze open --account "Trading"
./build/release/ibkr-options-analyzer analyze strategy --account "IRA"

# Generate report for all accounts
./build/release/ibkr-options-analyzer report --output all_accounts.csv

# Generate report for specific account
./build/release/ibkr-options-analyzer report \
  --account "Trading" \
  --output trading_only.csv
```

## Tips & Best Practices

### Daily Workflow
```bash
# Create a daily script
cat > ~/daily_options_check.sh << 'EOF'
#!/bin/bash
cd ~/Projects/ibkr-options-analyzer
./build/release/ibkr-options-analyzer download \
  --token "$IBKR_TOKEN" \
  --query-id "$IBKR_QUERY_ID" \
  --account "Main"
./build/release/ibkr-options-analyzer import
./build/release/ibkr-options-analyzer analyze strategy
EOF

chmod +x ~/daily_options_check.sh
```

### Check Expiring Positions
```bash
# View all positions (sorted by expiry)
./build/release/ibkr-options-analyzer analyze open

# The output shows days to expiry for each position
# Look for positions expiring soon (< 7 days)
# Consolidated summary shows:
# - Positions expiring in 7 days
# - Positions expiring in 30 days
```

### Monitor Assignment Risk
```bash
# View positions with real-time prices
./build/release/ibkr-options-analyzer analyze open

# Watch for risk indicators:
# ⚠️  ITM - In The Money (immediate assignment risk)
# ⚡ Near strike - Within 5% of strike (watch closely)

# Consolidated summary shows total positions at risk
```

### Risk Analysis
```bash
# View all strategies with risk metrics
./build/release/ibkr-options-analyzer analyze strategy

# Check the consolidated summary at the bottom:
# - Total Max Profit
# - Total Max Loss
# - Capital at Risk
# - Positions expiring soon
```

### Portfolio Overview
```bash
# Get complete portfolio snapshot
./build/release/ibkr-options-analyzer analyze open

# Consolidated summary includes:
# - Position breakdown (short puts, short calls, long)
# - Expiration schedule
# - Total premium collected
# - Total max loss
# - Positions at risk count
```

### Export for Spreadsheet Analysis
```bash
# Generate CSV report
./build/release/ibkr-options-analyzer report --output report.csv

# Open in Excel/Google Sheets for further analysis
# Includes: positions, strategies, risk metrics
```
