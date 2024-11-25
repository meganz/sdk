// Extracted from http://bugs.debian.org/797228

#include <atomic>
#include <cstdint>

int main()
{
    std::atomic<int64_t> a{};
    int64_t v = 5;
    int64_t r = a.fetch_add(v);
    return static_cast<int>(r);
}
