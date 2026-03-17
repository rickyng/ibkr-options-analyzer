#pragma once

#include "utils/result.hpp"
#include "utils/http_client.hpp"
#include "config/config_manager.hpp"
#include <string>
#include <chrono>

namespace ibkr::flex {

/**
 * Response from IBKR Flex Web Service SendRequest.
 */
struct SendRequestResponse {
    std::string reference_code;
    std::string status;
    std::string url;
};

/**
 * Response from IBKR Flex Web Service GetStatement.
 */
struct GetStatementResponse {
    enum class Status {
        Success,
        Pending,
        Failed,
        Unknown
    };

    Status status;
    std::string csv_content;
    std::string error_message;
};

/**
 * IBKR Flex Web Service client for downloading Flex reports.
 *
 * Implements the two-step Flex API flow:
 * 1. SendRequest: Initiate report generation, get ReferenceCode
 * 2. GetStatement: Poll for completion, download CSV when ready
 *
 * Endpoints (2026):
 * - SendRequest: https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/SendRequest
 * - GetStatement: https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/GetStatement
 *
 * Usage:
 *   FlexDownloader downloader(config);
 *   auto result = downloader.download_report(account_config);
 *   if (result) {
 *       std::cout << "CSV saved to: " << *result << "\n";
 *   }
 */
class FlexDownloader {
public:
    /**
     * Constructor
     * @param config Application configuration
     */
    explicit FlexDownloader(const config::Config& config);

    /**
     * Download Flex report for a single account.
     *
     * @param account Account configuration (token, query_id)
     * @param output_dir Directory to save CSV file (default: ~/.ibkr-options-analyzer/downloads)
     * @return Result containing path to downloaded CSV file or Error
     */
    utils::Result<std::string> download_report(
        const config::AccountConfig& account,
        const std::string& output_dir = "");

    /**
     * Download reports for all enabled accounts.
     *
     * @param output_dir Directory to save CSV files
     * @return Result containing vector of downloaded file paths or Error
     */
    utils::Result<std::vector<std::string>> download_all_reports(
        const std::string& output_dir = "");

private:
    const config::Config& config_;
    std::unique_ptr<utils::HttpClient> http_client_;

    // Flex API endpoints
    static constexpr const char* FLEX_BASE_URL = "https://ndcdyn.interactivebrokers.com";
    static constexpr const char* SEND_REQUEST_PATH = "/AccountManagement/FlexWebService/SendRequest";
    static constexpr const char* GET_STATEMENT_PATH = "/Universal/servlet/FlexStatementService.GetStatement";

    /**
     * Step 1: Send request to initiate report generation.
     *
     * @param token Flex Web Service token
     * @param query_id Flex Query ID
     * @return Result containing SendRequestResponse or Error
     */
    utils::Result<SendRequestResponse> send_request(
        const std::string& token,
        const std::string& query_id);

    /**
     * Step 2: Poll for report completion and download CSV.
     *
     * @param token Flex Web Service token
     * @param reference_code Reference code from SendRequest
     * @param base_url Optional base URL (from SendRequest response)
     * @return Result containing GetStatementResponse or Error
     */
    utils::Result<GetStatementResponse> get_statement(
        const std::string& token,
        const std::string& reference_code,
        const std::string& base_url = "");

    /**
     * Poll GetStatement until report is ready or timeout.
     *
     * @param token Flex Web Service token
     * @param reference_code Reference code from SendRequest
     * @param base_url Optional base URL (from SendRequest response)
     * @return Result containing CSV content or Error
     */
    utils::Result<std::string> poll_until_ready(
        const std::string& token,
        const std::string& reference_code,
        const std::string& base_url = "");

    /**
     * Parse XML response from SendRequest.
     *
     * Expected format:
     * <FlexStatementResponse>
     *   <Status>Success</Status>
     *   <ReferenceCode>1234567890</ReferenceCode>
     *   <Url>https://...</Url>
     * </FlexStatementResponse>
     *
     * @param xml_content XML response body
     * @return Result containing SendRequestResponse or Error
     */
    utils::Result<SendRequestResponse> parse_send_request_response(
        const std::string& xml_content);

    /**
     * Parse XML response from GetStatement.
     *
     * Expected formats:
     * Success: <FlexQueryResponse>...<FlexStatements>CSV_DATA</FlexStatements>...</FlexQueryResponse>
     * Pending: <FlexStatementResponse><Status>Pending</Status></FlexStatementResponse>
     * Error: <FlexStatementResponse><Status>Fail</Status><ErrorMessage>...</ErrorMessage></FlexStatementResponse>
     *
     * @param xml_content XML response body
     * @return Result containing GetStatementResponse or Error
     */
    utils::Result<GetStatementResponse> parse_get_statement_response(
        const std::string& xml_content);

    /**
     * Save CSV content to file.
     *
     * @param csv_content CSV data
     * @param output_dir Output directory
     * @param account_name Account name (for filename)
     * @return Result containing file path or Error
     */
    utils::Result<std::string> save_csv_file(
        const std::string& csv_content,
        const std::string& output_dir,
        const std::string& account_name);

    /**
     * Get default download directory (~/.ibkr-options-analyzer/downloads)
     */
    std::string get_default_download_dir() const;

    /**
     * Generate filename for downloaded CSV (includes timestamp)
     */
    std::string generate_filename(const std::string& account_name) const;
};

} // namespace ibkr::flex
