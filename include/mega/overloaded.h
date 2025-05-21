#pragma once

namespace mega
{

/**
 * @brief helper type for std::visit
 *
 * @example Usage example (see https://en.cppreference.com/w/cpp/utility/variant/visit):
 *   std::visit(overloaded{
 *          [](auto arg) { std::cout << arg << ' '; },
 *          [](double arg) { std::cout << std::fixed << arg << ' '; },
 *          [](const std::string& arg) { std::cout << std::quoted(arg) << ' '; }
 *      }, v);
 */
template<class... Ts>
struct overloaded: Ts...
{
    using Ts::operator()...;
};

// explicit deduction guide (not needed as of C++20)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // mega
