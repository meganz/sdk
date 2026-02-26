#include <cstdint>
#include <cstdlib>
#include <ctime>

template<typename T>
struct Test;

template<>
struct Test<std::int64_t>
{};

template<>
struct Test<time_t>
{};

int main()
{
    return EXIT_SUCCESS;
}
