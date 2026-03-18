# Changes Summary - Command-Line Credentials

## Overview
Modified IBKR Options Analyzer to accept account credentials via command-line arguments instead of requiring them in the config file. This improves security and flexibility.

## Changes Made

### 1. Code Changes

#### `src/config/config_manager.cpp`
- Made `accounts` array optional in config JSON parsing
- Removed validation requiring at least one account in config
- Accounts can now be empty (provided via CLI instead)

#### `src/main.cpp`
- Updated logging to handle empty accounts array
- Download command already supported `--token`, `--query-id`, `--account` arguments

#### `src/commands/download_command.cpp`
- Already implemented to accept credentials via parameters
- No changes needed

### 2. Configuration Files

#### `config.json.minimal` (NEW)
Minimal configuration file with only required settings:
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

#### `config.json.example` (UNCHANGED)
Kept for users who prefer storing credentials in config (legacy approach)

### 3. Documentation Updates

#### `USER_GUIDE.md`
- Updated setup instructions to use minimal config
- Changed all examples to show command-line credential usage
- Added environment variable examples for automation
- Updated security notes
- Removed references to storing tokens in config

#### `README.md`
- Updated Quick Start section
- Changed download command examples to use CLI args
- Added environment variable usage
- Updated troubleshooting section

#### `QUICK_REFERENCE.md` (NEW)
Quick reference card with common commands and usage patterns

### 4. Setup Script

#### `setup.sh` (NEW)
Automated setup script that:
- Creates config directory structure
- Generates minimal config.json
- Shows next steps with example commands

## Usage Changes

### Before (Config-based)
```bash
# Edit config file with credentials
nano ~/.ibkr-options-analyzer/config.json

# Download
./build/release/ibkr-options-analyzer download
```

### After (Command-line)
```bash
# No credentials in config file needed
./setup.sh

# Download with CLI args
./build/release/ibkr-options-analyzer download \
  --token YOUR_TOKEN \
  --query-id YOUR_QUERY_ID \
  --account "Account Name"
```

## Benefits

1. **Security**: Credentials not stored in files
2. **Flexibility**: Easy to use different credentials per run
3. **Automation**: Works well with environment variables
4. **Multi-account**: Simple to switch between accounts
5. **CI/CD friendly**: Credentials can be injected at runtime

## Backward Compatibility

The tool still supports the old approach:
- Users can add `accounts` array to config.json
- `config.json.example` shows the legacy format
- Both approaches work simultaneously

## Files Created

- `config.json.minimal` - Minimal config template
- `setup.sh` - Automated setup script
- `QUICK_REFERENCE.md` - Quick reference guide

## Files Modified

- `src/config/config_manager.cpp` - Made accounts optional
- `src/main.cpp` - Updated logging for empty accounts
- `USER_GUIDE.md` - Complete rewrite for CLI approach
- `README.md` - Updated examples and instructions

## Testing

Build completed successfully:
```bash
cmake --preset release
cmake --build build/release
```

Help command verified:
```bash
./build/release/ibkr-options-analyzer download --help
```

Output shows required CLI arguments:
- `--token TEXT REQUIRED`
- `--query-id TEXT REQUIRED`
- `--account TEXT REQUIRED`
- `--force` (optional)
