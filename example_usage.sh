#!/bin/bash
# Example script for downloading and analyzing IBKR positions
# Usage: ./example_usage.sh

set -e

# Configuration - Set your credentials here or use environment variables
IBKR_TOKEN="${IBKR_TOKEN:-YOUR_TOKEN_HERE}"
IBKR_QUERY_ID="${IBKR_QUERY_ID:-YOUR_QUERY_ID_HERE}"
ACCOUNT_NAME="${ACCOUNT_NAME:-Main Account}"

# Path to the executable
IBKR_TOOL="./build/release/ibkr-options-analyzer"

# Check if executable exists
if [ ! -f "$IBKR_TOOL" ]; then
    echo "Error: Executable not found at $IBKR_TOOL"
    echo "Please build the project first:"
    echo "  cmake --preset release && cmake --build build/release"
    exit 1
fi

# Check if credentials are set
if [ "$IBKR_TOKEN" = "YOUR_TOKEN_HERE" ] || [ "$IBKR_QUERY_ID" = "YOUR_QUERY_ID_HERE" ]; then
    echo "Error: Please set your IBKR credentials"
    echo ""
    echo "Option 1: Edit this script and set IBKR_TOKEN and IBKR_QUERY_ID"
    echo "Option 2: Export environment variables:"
    echo "  export IBKR_TOKEN='your_token'"
    echo "  export IBKR_QUERY_ID='your_query_id'"
    echo "  export ACCOUNT_NAME='Your Account Name'"
    exit 1
fi

echo "=========================================="
echo "IBKR Options Analyzer - Example Usage"
echo "=========================================="
echo ""

# Step 1: Download positions
echo "Step 1: Downloading positions from IBKR..."
$IBKR_TOOL download \
  --token "$IBKR_TOKEN" \
  --query-id "$IBKR_QUERY_ID" \
  --account "$ACCOUNT_NAME"

echo ""
echo "✓ Download complete"
echo ""

# Step 2: Import to database
echo "Step 2: Importing data to database..."
$IBKR_TOOL import

echo ""
echo "✓ Import complete"
echo ""

# Step 3: Analyze open positions
echo "Step 3: Analyzing open positions..."
echo "=========================================="
$IBKR_TOOL analyze open

echo ""

# Step 4: Show detected strategies
echo "Step 4: Detecting strategies..."
echo "=========================================="
$IBKR_TOOL analyze strategy

echo ""

# Step 5: Generate report
echo "Step 5: Generating report..."
REPORT_FILE="report_$(date +%Y%m%d_%H%M%S).csv"
$IBKR_TOOL report --output "$REPORT_FILE"

echo ""
echo "✓ Report saved to: $REPORT_FILE"
echo ""

echo "=========================================="
echo "Analysis complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  - Review the report: $REPORT_FILE"
echo "  - Check logs: ~/.ibkr-options-analyzer/logs/app.log"
echo "  - View database: ~/.ibkr-options-analyzer/data.db"
