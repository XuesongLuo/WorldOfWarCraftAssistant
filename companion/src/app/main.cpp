#include "wowai/app/build_info.hpp"

#include <iostream>

int main() {
    std::cout << wowai::app::application_name() << ' ' << wowai::app::application_version()
              << '\n';
    return 0;
}
