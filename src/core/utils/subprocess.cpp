#include "subprocess.hpp"
#include "logger.hpp"
#include <cstdio>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace ibkr::utils {

Result<std::string> run_script(
    const std::string& script,
    const std::vector<std::string>& args,
    const std::string& stdin_data) {

    // Create a secure temp file using mkstemp (atomic, no race condition)
    std::string tmp_template = "/tmp/ibkr_input_XXXXXX.json";
    char tmp_path_buf[64];
    std::strncpy(tmp_path_buf, tmp_template.c_str(), sizeof(tmp_path_buf) - 1);
    tmp_path_buf[sizeof(tmp_path_buf) - 1] = '\0';

    int tmp_fd = mkstemp(tmp_path_buf);
    if (tmp_fd == -1) {
        return Error{"Failed to create secure temp file"};
    }

    // Write stdin data
    ssize_t written = write(tmp_fd, stdin_data.c_str(), stdin_data.size());
    close(tmp_fd);
    if (written != static_cast<ssize_t>(stdin_data.size())) {
        std::remove(tmp_path_buf);
        return Error{"Failed to write to temp file"};
    }

    // Determine python binary
    std::string python_bin = ".venv/bin/python3";
    if (!std::filesystem::exists(python_bin)) {
        python_bin = "python3";
    }

    // Build argv array (no shell interpolation)
    std::vector<std::string> argv_strings;
    argv_strings.push_back(python_bin);
    argv_strings.push_back(script);
    for (const auto& arg : args) {
        argv_strings.push_back(arg);
    }

    // Convert to char* array for execvp
    std::vector<char*> argv_ptrs;
    for (auto& s : argv_strings) {
        argv_ptrs.push_back(s.data());
    }
    argv_ptrs.push_back(nullptr);

    // Create pipe for stdout capture
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        std::remove(tmp_path_buf);
        return Error{"Failed to create pipe"};
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        std::remove(tmp_path_buf);
        return Error{"Failed to fork process"};
    }

    if (pid == 0) {
        // Child process
        int stdin_fd = open(tmp_path_buf, O_RDONLY);
        if (stdin_fd == -1) {
            _exit(127);
        }
        dup2(stdin_fd, STDIN_FILENO);
        close(stdin_fd);

        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        execvp(argv_ptrs[0], argv_ptrs.data());
        _exit(127);
    }

    // Parent process
    close(pipe_fds[1]);

    std::string output;
    std::array<char, 4096> buffer;
    ssize_t bytes;
    while ((bytes = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), bytes);
    }
    close(pipe_fds[0]);

    int status;
    waitpid(pid, &status, 0);

    std::remove(tmp_path_buf);

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (exit_code != 0) {
        Logger::error("Script failed (exit {}): {}", exit_code, output);
        return Error{"Script export failed", output};
    }

    // Trim trailing whitespace/newlines
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
        output.pop_back();
    }

    return output;
}

} // namespace ibkr::utils
