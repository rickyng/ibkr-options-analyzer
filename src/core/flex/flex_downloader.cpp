#include "flex_downloader.hpp"
#include "utils/logger.hpp"
#include <pugixml.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace ibkr::flex {

using utils::Result;
using utils::Error;
using utils::Logger;

FlexDownloader::FlexDownloader(const config::Config& config,
                               std::unique_ptr<utils::HttpClient> http_client)
    : config_(config) {

    if (http_client) {
        http_client_ = std::move(http_client);
    } else {
        http_client_ = std::make_unique<utils::HttpClient>(
            FLEX_BASE_URL,
            config_.http.user_agent,
            config_.http.timeout_seconds,
            config_.http.max_retries,
            config_.http.retry_delay_ms
        );
    }
}

Result<std::string> FlexDownloader::download_report(
    const config::AccountConfig& account,
    const std::string& output_dir) {

    Logger::info("Downloading Flex report for account: {}", account.name);

    // Step 1: Send request to initiate report generation
    auto send_result = send_request(account.token, account.query_id);
    if (!send_result) {
        return Error{
            "Failed to send Flex request",
            send_result.error().message,
            send_result.error().code
        };
    }

    const auto& send_response = *send_result;
    Logger::info("Flex request sent successfully. ReferenceCode: [REDACTED]");

    // Step 2: Poll until report is ready (use the URL from SendRequest response if provided)
    auto csv_result = poll_until_ready(account.token, send_response.reference_code, send_response.url);
    if (!csv_result) {
        return Error{
            "Failed to download Flex report",
            csv_result.error().message,
            csv_result.error().code
        };
    }

    // Step 3: Save CSV to file
    std::string dir = output_dir.empty() ? get_default_download_dir() : output_dir;
    auto save_result = save_csv_file(*csv_result, dir, account.name);
    if (!save_result) {
        return Error{
            "Failed to save CSV file",
            save_result.error().message
        };
    }

    Logger::info("Flex report downloaded successfully: {}", *save_result);
    return save_result;
}

Result<std::vector<std::string>> FlexDownloader::download_all_reports(
    const std::string& output_dir) {

    std::vector<std::string> downloaded_files;
    std::vector<std::string> failed_accounts;

    for (const auto& account : config_.accounts) {
        if (!account.enabled) {
            Logger::debug("Skipping disabled account: {}", account.name);
            continue;
        }

        auto result = download_report(account, output_dir);
        if (result) {
            downloaded_files.push_back(*result);
        } else {
            Logger::error("Failed to download report for account '{}': {}",
                         account.name, result.error().format());
            failed_accounts.push_back(account.name);
        }
    }

    if (downloaded_files.empty()) {
        return Error{"No reports downloaded successfully"};
    }

    if (!failed_accounts.empty()) {
        Logger::warn("Some accounts failed: {}", fmt::join(failed_accounts, ", "));
    }

    return downloaded_files;
}

Result<SendRequestResponse> FlexDownloader::send_request(
    const std::string& token,
    const std::string& query_id) {

    Logger::debug("Sending Flex request: query_id={}", query_id);

    // Build request URL with query parameters
    std::map<std::string, std::string> params;
    params["t"] = token;
    params["q"] = query_id;
    params["v"] = "3";  // API version 3

    Logger::debug("Request params: t=[REDACTED], q={}, v=3", query_id);

    auto http_result = http_client_->get(SEND_REQUEST_PATH, params);
    if (!http_result) {
        return Error{
            "HTTP request failed",
            http_result.error().message,
            http_result.error().code
        };
    }

    const auto& response = *http_result;

    // Check for HTTP errors
    if (response.status_code == 401) {
        return Error{
            "Authentication failed",
            "Token expired or invalid. Please regenerate your Flex Web Service token.",
            401
        };
    } else if (response.status_code == 403) {
        return Error{
            "Access forbidden",
            "IP address not authorized. Flex tokens are tied to your IP address.",
            403
        };
    } else if (response.status_code == 404) {
        return Error{
            "Query not found",
            "Query ID not found. Please verify your Flex Query ID in IBKR Account Management.",
            404
        };
    } else if (response.status_code >= 400) {
        return Error{
            "HTTP error",
            "Status code: " + std::to_string(response.status_code),
            response.status_code
        };
    }

    // Parse XML response
    return parse_send_request_response(response.body);
}

Result<GetStatementResponse> FlexDownloader::get_statement(
    const std::string& token,
    const std::string& reference_code,
    const std::string& url_from_response) {

    Logger::debug("Getting Flex statement: reference_code={}, token_length={}",
                 reference_code, token.length());
    Logger::debug("URL from SendRequest response: '{}'", url_from_response);

    // IMPORTANT: Always use the classic servlet path on ndcdyn, not the URL from SendRequest
    // The newer REST-style path returns error 1020, but the servlet path works
    Logger::debug("Using classic servlet path: {}{}", FLEX_BASE_URL, GET_STATEMENT_PATH);

    // Build request URL with query parameters
    std::string full_path = std::string(GET_STATEMENT_PATH) + "?t=" + token
                           + "&q=" + reference_code + "&v=3";

    Logger::debug("Request path (redacted): {}?t=[REDACTED]&q={}&v=3",
                  GET_STATEMENT_PATH, reference_code);

    auto http_result = http_client_->get(full_path, {});
    if (!http_result) {
        return Error{
            "HTTP request failed",
            http_result.error().message,
            http_result.error().code
        };
    }

    const auto& response = *http_result;

    // Log response for debugging
    Logger::debug("GetStatement HTTP status: {}", response.status_code);
    Logger::debug("GetStatement response headers count: {}", response.headers.size());
    Logger::debug("GetStatement response body length: {}", response.body.length());

    // Check for HTTP errors (same as SendRequest)
    if (response.status_code == 401) {
        return Error{"Authentication failed", "Token expired or invalid", 401};
    } else if (response.status_code == 403) {
        return Error{"Access forbidden", "IP address not authorized", 403};
    } else if (response.status_code >= 400) {
        return Error{
            "HTTP error",
            "Status code: " + std::to_string(response.status_code),
            response.status_code
        };
    }

    // Parse XML response
    auto parse_result = parse_get_statement_response(response.body);
    if (!parse_result) {
        Logger::error("Failed to parse GetStatement response: {}", parse_result.error().format());
    } else {
        Logger::debug("Parse successful, status: {}",
                     static_cast<int>(parse_result->status));
        if (parse_result->status == GetStatementResponse::Status::Failed) {
            Logger::error("IBKR returned Fail status. Error message: '{}'",
                         parse_result->error_message);
        }
    }
    return parse_result;
}

Result<std::string> FlexDownloader::poll_until_ready(
    const std::string& token,
    const std::string& reference_code,
    const std::string& base_url) {

    Logger::info("Polling for report completion (max {} seconds)...",
                config_.flex.max_poll_duration_seconds);

    auto start_time = std::chrono::steady_clock::now();
    auto max_duration = std::chrono::seconds(config_.flex.max_poll_duration_seconds);
    auto poll_interval = std::chrono::seconds(config_.flex.poll_interval_seconds);

    // Wait a bit before first poll to give IBKR time to process the request
    Logger::debug("Waiting {} seconds before first poll...", config_.flex.poll_interval_seconds);
    std::this_thread::sleep_for(poll_interval);

    int attempt = 0;
    while (true) {
        attempt++;

        // Check timeout
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= max_duration) {
            return Error{
                "Polling timeout",
                "Report generation took longer than " +
                std::to_string(config_.flex.max_poll_duration_seconds) + " seconds"
            };
        }

        // Get statement (use custom base URL if provided)
        auto result = get_statement(token, reference_code, base_url);
        if (!result) {
            return Error{
                "Failed to get statement",
                result.error().message,
                result.error().code
            };
        }

        const auto& statement = *result;

        // Check status
        if (statement.status == GetStatementResponse::Status::Success) {
            Logger::info("Report ready after {} attempts ({:.1f} seconds)",
                        attempt,
                        std::chrono::duration<double>(elapsed).count());
            return statement.csv_content;
        } else if (statement.status == GetStatementResponse::Status::Failed) {
            return Error{
                "Report generation failed",
                statement.error_message.empty() ? "Unknown error" : statement.error_message
            };
        } else if (statement.status == GetStatementResponse::Status::Pending) {
            Logger::debug("Report pending (attempt {})...", attempt);
            std::this_thread::sleep_for(poll_interval);
            continue;
        } else {
            return Error{
                "Unknown status",
                "Unexpected response from Flex API"
            };
        }
    }
}

Result<SendRequestResponse> FlexDownloader::parse_send_request_response(
    const std::string& xml_content) {

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml_content.c_str(), pugi::parse_minimal);

    if (!parse_result) {
        return Error{
            "XML parse error",
            std::string(parse_result.description()),
            static_cast<int>(parse_result.offset)
        };
    }

    // Expected structure: <FlexStatementResponse><Status>...</Status><ReferenceCode>...</ReferenceCode></FlexStatementResponse>
    auto root = doc.child("FlexStatementResponse");
    if (!root) {
        return Error{"Invalid XML", "Missing FlexStatementResponse root element"};
    }

    SendRequestResponse response;

    // Parse Status
    auto status_node = root.child("Status");
    if (status_node) {
        response.status = status_node.child_value();
    }

    // Parse ReferenceCode
    auto ref_code_node = root.child("ReferenceCode");
    if (!ref_code_node) {
        // Check for error message
        auto error_node = root.child("ErrorMessage");
        if (error_node) {
            return Error{
                "Flex API error",
                error_node.child_value()
            };
        }
        return Error{"Invalid XML", "Missing ReferenceCode in response"};
    }
    response.reference_code = ref_code_node.child_value();

    // Parse URL (optional)
    auto url_node = root.child("Url");
    if (url_node) {
        response.url = url_node.child_value();
    }

    if (response.reference_code.empty()) {
        return Error{"Invalid response", "ReferenceCode is empty"};
    }

    return response;
}

Result<GetStatementResponse> FlexDownloader::parse_get_statement_response(
    const std::string& xml_content) {

    // Check if response is CSV (starts with quotes or column headers)
    if (!xml_content.empty() && (xml_content[0] == '"' || xml_content.substr(0, 10).find("Client") != std::string::npos)) {
        Logger::debug("Response is CSV format (not XML)");
        GetStatementResponse response;
        response.status = GetStatementResponse::Status::Success;
        response.csv_content = xml_content;
        return response;
    }

    // Try to parse as XML
    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_string(xml_content.c_str(), pugi::parse_minimal);

    if (!parse_result) {
        // If XML parsing fails and content looks like CSV, treat as CSV
        if (xml_content.find(',') != std::string::npos && xml_content.find('\n') != std::string::npos) {
            Logger::debug("XML parse failed but content looks like CSV, treating as CSV");
            GetStatementResponse response;
            response.status = GetStatementResponse::Status::Success;
            response.csv_content = xml_content;
            return response;
        }

        return Error{
            "XML parse error",
            std::string(parse_result.description()),
            static_cast<int>(parse_result.offset)
        };
    }

    GetStatementResponse response;

    // Check for FlexStatementResponse (status response)
    auto status_root = doc.child("FlexStatementResponse");
    if (status_root) {
        auto status_node = status_root.child("Status");
        if (status_node) {
            std::string status_str = status_node.child_value();
            if (status_str == "Success") {
                response.status = GetStatementResponse::Status::Success;
            } else if (status_str == "Pending" || status_str == "Warn") {
                response.status = GetStatementResponse::Status::Pending;
            } else if (status_str == "Fail") {
                response.status = GetStatementResponse::Status::Failed;
                auto error_node = status_root.child("ErrorMessage");
                if (error_node) {
                    response.error_message = error_node.child_value();
                }
            } else {
                response.status = GetStatementResponse::Status::Unknown;
            }
        }
        return response;
    }

    // Check for FlexQueryResponse (actual data)
    auto data_root = doc.child("FlexQueryResponse");
    if (data_root) {
        response.status = GetStatementResponse::Status::Success;

        // The CSV data is embedded in the XML
        // For simplicity, we'll return the entire XML content as CSV
        // In production, you might want to extract specific sections
        response.csv_content = xml_content;

        return response;
    }

    return Error{"Invalid XML", "Unknown response format"};
}

Result<std::string> FlexDownloader::save_csv_file(
    const std::string& csv_content,
    const std::string& output_dir,
    const std::string& account_name) {

    // Create output directory if it doesn't exist
    std::filesystem::path dir_path(output_dir);
    try {
        std::filesystem::create_directories(dir_path);
    } catch (const std::exception& e) {
        return Error{
            "Failed to create directory",
            std::string(e.what())
        };
    }

    // Generate filename
    std::string filename = generate_filename(account_name);
    std::filesystem::path file_path = dir_path / filename;

    // Write file
    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        return Error{
            "Failed to open file for writing",
            file_path.string()
        };
    }

    file << csv_content;
    file.close();

    if (!file) {
        return Error{
            "Failed to write file",
            file_path.string()
        };
    }

    return file_path.string();
}

std::string FlexDownloader::get_default_download_dir() const {
    return config::ConfigManager::expand_path("~/.ibkr-options-analyzer/downloads");
}

std::string FlexDownloader::generate_filename(const std::string& account_name) const {
    // Generate filename: flex_report_AccountName_YYYYMMDD_HHMMSS.csv
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << "flex_report_";

    // Sanitize account name (replace spaces with underscores)
    std::string sanitized_name = account_name;
    std::replace(sanitized_name.begin(), sanitized_name.end(), ' ', '_');
    oss << sanitized_name << "_";

    // Add timestamp (use thread-safe localtime_r on POSIX)
    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf);
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    oss << ".csv";

    return oss.str();
}

} // namespace ibkr::flex
