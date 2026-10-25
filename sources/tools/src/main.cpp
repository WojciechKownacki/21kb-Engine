#include "CliProgram.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// kb_cli talks UTF-8: arguments arrive in UTF-8 (kb_cli.manifest sets the process code page on
// Windows; Linux is UTF-8 already) and names with any letters are printed as such. The console's
// own code page is restored on exit.
class Utf8Console final {
public:
    Utf8Console() {
#if defined(_WIN32)
        previous_ = GetConsoleOutputCP();
        if (previous_ != 0U) {
            static_cast<void>(SetConsoleOutputCP(CP_UTF8));
        }
#endif
    }
    ~Utf8Console() {
#if defined(_WIN32)
        if (previous_ != 0U) {
            static_cast<void>(SetConsoleOutputCP(previous_));
        }
#endif
    }
    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

private:
    unsigned int previous_ = 0U;
};

int main(int argc, char** argv) {
    const Utf8Console console;
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        return kb::cli::RunCli(arguments, std::cin, kb::cli::CommandIo{ .out = std::cout, .err = std::cerr });
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "error: unexpected failure\n";
    }
    return 1;
}
