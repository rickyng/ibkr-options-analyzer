#pragma once

#include <string>

namespace ibkr::parser {

struct StockTradeRecord {
    std::string symbol;
    std::string description;
    std::string trade_date;
    double quantity = 0;
    double trade_price = 0;
    double proceeds = 0;
    double commission = 0;
    double net_cash = 0;
    std::string notes_codes;
};

struct DividendRecord {
    std::string symbol;
    std::string description;
    std::string ex_date;
    std::string pay_date;
    double amount = 0;
    double tax_withheld = 0;
    std::string currency = "USD";
};

struct InterestRecord {
    std::string currency;
    std::string date;
    std::string description;
    double amount = 0;
};

} // namespace ibkr::parser
