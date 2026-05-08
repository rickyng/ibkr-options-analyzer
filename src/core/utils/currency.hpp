#pragma once

#include <string>
#include <map>
#include <cmath>
#include <cctype>

namespace ibkr::utils {

/**
 * Simple currency converter.
 * Uses approximate exchange rates for normalizing premiums to USD.
 * Rates can be updated periodically.
 */
class CurrencyConverter {
public:
    CurrencyConverter() = default;
    explicit CurrencyConverter(const std::string& base_currency)
        : base_currency_(base_currency.empty() ? "USD" : base_currency) {}

    double convert(double amount, const std::string& from) const {
        if (from == base_currency_) return amount;
        auto it = rates_.find(from);
        if (it != rates_.end()) return amount * it->second;
        return amount;
    }

    std::string get_base_currency() const { return base_currency_; }

    void update_rate(const std::string& currency, double rate_to_base) {
        rates_[currency] = rate_to_base;
    }

private:
    std::string base_currency_{"USD"};
    std::map<std::string, double> rates_{
        {"USD", 1.0},
        {"HKD", 0.128},
        {"JPY", 0.0067},
        {"EUR", 1.08},
        {"GBP", 1.25},
        {"CAD", 0.74},
        {"AUD", 0.65},
        {"CHF", 1.12},
        {"SGD", 0.75},
    };
};

/**
 * Deduce currency from underlying symbol.
 * Patterns:
 *   - Ends with .T  → JPY  (e.g., 1321.T, 6758.T)
 *   - Ends with .HK → HKD  (e.g., 0700.HK)
 *   - Ends with .TO → CAD  (e.g., RY.TO)
 *   - Numeric-only (≤5 digits) → HKD (IBKR HK tickers)
 *   - Otherwise → USD
 */
inline std::string deduce_currency(const std::string& underlying) {
    if (underlying.empty()) return "USD";

    // Check suffix patterns
    if (underlying.size() > 2) {
        std::string suffix = underlying.substr(underlying.size() - 2);
        if (suffix == ".T") return "JPY";
    }
    if (underlying.size() > 3) {
        std::string suffix = underlying.substr(underlying.size() - 3);
        if (suffix == ".HK") return "HKD";
        if (suffix == ".TO") return "CAD";
    }

    // Numeric-only symbols (IBKR HK tickers are plain numbers like 1299, 388)
    bool is_numeric = !underlying.empty();
    for (char c : underlying) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            is_numeric = false;
            break;
        }
    }
    if (is_numeric && underlying.length() <= 5) return "HKD";

    return "USD";
}

/**
 * Get currency symbol for a currency code.
 */
inline std::string get_currency_symbol(const std::string& currency) {
    static const std::map<std::string, std::string> symbols = {
        {"USD", "$"},
        {"EUR", "€"},
        {"GBP", "£"},
        {"JPY", "¥"},
        {"CNY", "¥"},
        {"CAD", "C$"},
        {"AUD", "A$"},
        {"CHF", "Fr"},
        {"HKD", "HK$"},
    };
    auto it = symbols.find(currency);
    if (it != symbols.end()) return it->second;
    return currency + " ";
}

} // namespace ibkr::utils
