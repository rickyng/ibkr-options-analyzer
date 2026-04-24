# Findings — IBKR Options Analyzer

## Codebase Exploration (updated 2026-04-24)

### Current Architecture

```
src/core/          — Static library (libibkr_core.a)
  config/          — ConfigManager, config loading
  flex/            — FlexDownloader, IBKR Flex Web Service client
  parsers/         — Option symbol parser, CSV parser
  db/              — Database, schema, CRUD operations
  analysis/        — StrategyDetector, RiskCalculator
  report/          — ReportGenerator, CSVExporter
  utils/           — Logger, HttpClient, PriceFetcher, JsonOutput, Result<T>
  services/        — FlexService, ImportService, PositionService, PriceService,
                     StrategyService, ReportService
src/cli/           — CLI executable
  main.cpp         — CLI11 setup, --format json, --quiet flags
  commands/        — Thin wrappers delegating to services
```

### Completed Phases

- **Phase 1**: Core static library + CLI executable separation
- **Phase 2**: Analysis module (StrategyDetector, RiskCalculator with multi-account support)
- **Phase 3**: JSON output layer (--format json, --quiet, stderr logging in JSON mode)
- **Phase 4**: Service layer extraction (6 service classes, commands are thin wrappers)

### What's Still Pending

- Unit test suite (Catch2)
- CLI multi-account support (--account flag for download/import)
- Python dashboard project (FastAPI + Dash, reads SQLite, calls CLI for writes)

### Build

- CMake presets: debug (with sanitizers), release (optimized)
- Dependencies: CLI11, fmt, spdlog, nlohmann_json, pugixml, cpp-httplib, rapidcsv, SQLiteCpp, date
- C++20 standard
