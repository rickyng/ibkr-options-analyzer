#pragma once

#include <string>
#include <vector>
#include "utils/result.hpp"

namespace ibkr::utils {

/**
 * Run an external script with stdin data and capture stdout.
 *
 * @param script  Path to the script to execute
 * @param args    Command-line arguments
 * @param stdin_data  Data to pipe to the process's stdin
 * @return The script's stdout output, or an error
 */
Result<std::string> run_script(
    const std::string& script,
    const std::vector<std::string>& args,
    const std::string& stdin_data);

} // namespace ibkr::utils