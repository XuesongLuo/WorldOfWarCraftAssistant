#include "wowai/codex/mock_host.hpp"

#include <iostream>
#include <string>

int main() {
    wowai::codex::DeterministicMockHost host;
    std::string line;
    while (std::getline(std::cin, line)) {
        nlohmann::json output;
        const auto result = host.handle_line(line, output);
        if (!result) {
            std::cerr << result.message << '\n';
            return 2;
        }
        std::cout << output.dump() << '\n';
    }
    return 0;
}
