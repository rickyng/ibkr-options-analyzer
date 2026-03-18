# Migration Complete: Command-Line Credentials

## Summary

Successfully migrated IBKR Options Analyzer from config-file-based credentials to command-line arguments.

## What Changed

### Core Changes
✓ Made `accounts` array optional in config.json
✓ Credentials now passed via `--token`, `--query-id`, `--account` flags
✓ Config file only needs database and logging settings
✓ Backward compatible with old config format

### New Files
✓ `config.json.minimal` - Minimal config template
✓ `setup.sh` - Automated setup script
✓ `example_usage.sh` - Complete usage example
✓ `QUICK_REFERENCE.md` - Quick command reference
✓ `CHANGES.md` - Detailed change log

### Updated Documentation
✓ `USER_GUIDE.md` - Complete rewrite for CLI approach
✓ `README.md` - Updated examples and instructions

## Quick Start (New Users)

```bash
# 1. Build
cmake --preset release && cmake --build build/release

# 2. Setup
./setup.sh

# 3. Download positions
./build/release/ibkr-options-analyzer download \
  --token YOUR_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Account Name"

# 4. Analyze
./build/release/ibkr-options-analyzer analyze open
```

## For Automation

```bash
# Set environment variables
export IBKR_TOKEN="your_token"
export IBKR_QUERY_ID="your_query_id"
export ACCOUNT_NAME="Main Account"

# Run example script
./example_usage.sh
```

## Benefits

1. **Security**: No credentials stored in files
2. **Flexibility**: Easy to switch accounts
3. **Automation**: Works with env vars and CI/CD
4. **Multi-account**: Run command multiple times with different creds

## Verification

✓ Build completed successfully
✓ Help command shows required arguments
✓ All documentation updated
✓ Example scripts created and tested

## Next Steps for Users

1. Get IBKR Flex token and query ID
2. Run `./setup.sh`
3. Use download command with credentials
4. See `QUICK_REFERENCE.md` for common commands
