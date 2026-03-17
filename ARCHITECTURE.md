# Architecture Overview

## High-Level Component Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                          CLI Entry Point                             │
│                         (main.cpp + CLI11)                           │
└────────────┬────────────────────────────────────────────────────────┘
             │
             ├─── download ──────────────────────────────────────┐
             │                                                    │
             ├─── import ────────────────────────────────────┐   │
             │                                                │   │
             ├─── manual-add ────────────────────────────┐   │   │
             │                                            │   │   │
             ├─── analyze [open|impact|strategy] ────┐   │   │   │
             │                                        │   │   │   │
             └─── report ────────────────────────┐   │   │   │   │
                                                  │   │   │   │   │
                                                  ▼   ▼   ▼   ▼   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Command Handlers                             │
│  (download_command, import_command, manual_add_command, etc.)       │
└────────┬────────────────────────────────────────────────────────────┘
         │
         ├──────────────────────────────────────────────────────┐
         │                                                       │
         ▼                                                       ▼
┌──────────────────────┐                            ┌──────────────────┐
│   Config Manager     │                            │   Logger         │
│  (config.json load)  │                            │  (spdlog)        │
└──────────────────────┘                            └──────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                        Flex Downloader                                │
│  1. SendRequest (POST token + query_id)                              │
│  2. Parse XML → extract ReferenceCode                                │
│  3. Poll GetStatement (retry with exponential backoff)               │
│  4. Save CSV to disk                                                 │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         CSV Parser                                    │
│  - Parse Trades section (TradeDate, Symbol, Quantity, etc.)         │
│  - Parse OpenPositions section (Symbol, Quantity, MarkPrice, etc.)  │
│  - Filter: only options (detect via symbol format)                   │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    Option Symbol Parser                               │
│  Input:  "AAPL  250321C00150000" or "AAPL250321C150"                │
│  Output: {underlying: "AAPL", expiry: "2025-03-21",                 │
│           strike: 150.0, right: 'C'}                                 │
│  - Handle multiple IBKR symbol formats                               │
│  - Validate expiry > current_date (using date library)               │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         Database Layer                                │
│  - SQLite via SQLiteCpp                                              │
│  - Tables: accounts, trades, open_options, detected_strategies       │
│  - CRUD operations with parameterized queries                        │
│  - Batch inserts for performance                                     │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                      Strategy Detector                                │
│  Rules-based detection:                                              │
│  - Naked Short Put: single short put, no other legs                  │
│  - Short Put Spread: short put + long put (lower strike)            │
│  - Covered Call: long stock + short call                             │
│  - Iron Condor: short put spread + short call spread                 │
│  Group by: (account, underlying, expiry)                             │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       Risk Calculator                                 │
│  Per position:                                                       │
│  - Breakeven price (strike - premium for short put)                  │
│  - Max profit (premium collected)                                    │
│  - Max loss (strike * 100 - premium for naked short put)            │
│  - Distance to strike (%)                                            │
│  - Delta approximation (if mark price available)                     │
│  Portfolio-level:                                                    │
│  - Total net premium at risk                                         │
│  - Total max theoretical loss                                        │
│  - Grouped by underlying/strategy/account                            │
└────────┬─────────────────────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────────────────┐
│                      Report Generator                                 │
│  - Console output (colored tables via fmt)                           │
│  - CSV export for Excel/Google Sheets                                │
│  - Summary statistics (total premium, max loss, etc.)                │
└──────────────────────────────────────────────────────────────────────┘
```

## Data Flow Example: Download → Analyze

1. **User runs**: `ibkr-options-analyzer download --account "Main Account"`
2. **ConfigManager** loads `~/.ibkr-options-analyzer/config.json`
3. **FlexDownloader** sends request to IBKR Flex Web Service
4. **FlexDownloader** polls until CSV is ready (max 5 minutes)
5. **CSVParser** parses Trades + OpenPositions sections
6. **OptionSymbolParser** extracts underlying/expiry/strike/right from each symbol
7. **Database** imports non-expired options into `open_options` table
8. **User runs**: `ibkr-options-analyzer analyze open`
9. **StrategyDetector** groups positions and detects strategies
10. **RiskCalculator** computes breakeven, max profit/loss for each position
11. **ReportGenerator** displays colored table in terminal

## Error Handling Strategy

- **Result<T, E>** type for all fallible operations (config load, HTTP requests, parsing, DB operations)
- **Retry logic** with exponential backoff for Flex polling (network transient errors)
- **Validation** at boundaries:
  - Config: validate token format, query_id format, paths exist
  - CSV: validate required columns exist, data types correct
  - Manual-add: validate expiry format (YYYYMMDD), strike > 0, right in {C, P}
- **Graceful degradation**: if one account fails, continue with others (log error)
- **User-friendly errors**: "Token expired for account 'Main Account'" not "HTTP 401"

## Extensibility Points

1. **New data sources**: Add new parser in `parser/` (e.g., TWS API, manual CSV)
2. **New strategies**: Extend `StrategyDetector` with new rules
3. **New risk metrics**: Extend `RiskCalculator` (e.g., Black-Scholes Greeks)
4. **New output formats**: Extend `ReportGenerator` (e.g., JSON, HTML)
5. **Live pricing**: Add TWS API client in `flex/` or separate module

## Performance Considerations

- **Batch inserts**: Insert 1000+ trades in single transaction
- **Lazy loading**: Only load non-expired options (filter in SQL)
- **Indexes**: On (account_id, underlying, expiry) for fast queries
- **Connection pooling**: Reuse HTTP connections for Flex polling
- **Parallel downloads**: Download multiple accounts concurrently (future)

## Security Considerations

- **No hard-coded secrets**: All tokens in config.json (user-managed)
- **File permissions**: config.json should be 0600 (user-only read/write)
- **SQL injection**: Parameterized queries via SQLiteCpp
- **HTTPS only**: All Flex API calls use HTTPS
- **User-Agent**: Required by IBKR, identifies tool as personal use
