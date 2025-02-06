/**
 * @brief Unitary test for generic cache LRU
 */

#include <gtest/gtest.h>
#include <mega/types.h>

void addElementToLRU(LRUCache<int, std::string>& lru, int element)
{
    lru.put(element, std::to_string(element));
    auto optionalElement = lru.get(element);
    ASSERT_TRUE(optionalElement.has_value());
    ASSERT_EQ(optionalElement.value(), std::to_string(element));
}

/**
 * @brief Test container LRUCache adding elements
 */
TEST(LRUCache, addElements)
{
    std::vector<int> elements{1, 2, 3, 4};
    LRUCache<int, std::string> lru(elements.size());

    for (const auto& element: elements)
    {
        addElementToLRU(lru, element);
    }

    ASSERT_FALSE(lru.get(5).has_value());
}

/**
 * @brief Test container LRUCache adding elements and exceeding the size
 *
 * First element added should be removed
 */
TEST(LRUCache, addElementsExceedingSize)
{
    std::vector<int> elements{1, 2, 3, 4};
    LRUCache<int, std::string> lru(elements.size() - 1);

    for (const auto& element: elements)
    {
        addElementToLRU(lru, element);
    }

    // First element has been removed
    ASSERT_FALSE(lru.get(elements[0]).has_value());
}

/**
 * @brief Test container LRUCache adding elements and exceeding the size
 *
 * Unlike the previous test, here the first element is accessed before any purge occurs.
 * As a result, the first element remains in the cache while the second is removed.
 *
 * First element added should be removed
 */
TEST(LRUCache, addElementsExceedingSizeV2)
{
    LRUCache<int, std::string> lru(3);

    addElementToLRU(lru, 1);
    addElementToLRU(lru, 2);
    addElementToLRU(lru, 3);
    addElementToLRU(lru, 1);

    // Exceed size
    addElementToLRU(lru, 4);

    ASSERT_TRUE(lru.get(1).has_value());
    ASSERT_FALSE(lru.get(2).has_value());
}
