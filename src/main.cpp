#include <iostream>
#include <string_view>

namespace rdvc {

constexpr std::string_view phase0_message()
{
    return "RDVC Phase 0: C++20/CMake environment check";
}

} // namespace rdvc

int main()
{
    std::cout << rdvc::phase0_message() << '\n';
    std::cout << "__cplusplus = " << __cplusplus << '\n';
    return 0;
}
