#!/bin/bash
# Setup script for IBKR Options Analyzer

set -e

echo "Setting up IBKR Options Analyzer..."

# Create config directory
CONFIG_DIR="$HOME/.ibkr-options-analyzer"
mkdir -p "$CONFIG_DIR"
mkdir -p "$CONFIG_DIR/logs"

# Copy minimal config if it doesn't exist
if [ ! -f "$CONFIG_DIR/config.json" ]; then
    echo "Creating minimal config file..."
    cat > "$CONFIG_DIR/config.json" << 'EOF'
{
  "database": {
    "path": "~/.ibkr-options-analyzer/data.db"
  },
  "logging": {
    "level": "info",
    "file": "~/.ibkr-options-analyzer/logs/app.log"
  }
}
EOF
    echo "✓ Config file created at $CONFIG_DIR/config.json"
else
    echo "✓ Config file already exists at $CONFIG_DIR/config.json"
fi

echo ""
echo "Setup complete!"
echo ""
echo "Next steps:"
echo "1. Get your IBKR Flex token and query ID from:"
echo "   https://www.interactivebrokers.com/portal"
echo ""
echo "2. Run the download command:"
echo "   ./build/release/ibkr-options-analyzer download \\"
echo "     --token YOUR_TOKEN \\"
echo "     --query-id YOUR_QUERY_ID \\"
echo "     --account \"Account Name\""
echo ""
echo "See USER_GUIDE.md for detailed instructions."
