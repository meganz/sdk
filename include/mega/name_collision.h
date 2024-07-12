/**
 * @file
 * @brief This file defines a set of utilities to deal with name collisions
 *
 * Namespace -> mega::ncoll(name collision)
 */

#ifndef MEGA_PWM_IMPORT_NAME_COLLISION_H
#define MEGA_PWM_IMPORT_NAME_COLLISION_H 1

#include <map>
#include <string>
#include <vector>

namespace mega::ncoll
{
using nameId_t = unsigned int; // Type for numbers attached to names when a collision is found

enum class ENameType
{
    baseNameOnly, // "test"
    withIdNoSpace, // "test(1)"
    withIdSpace // "test (1)"
};

/**
 * @brief Takes a name for a potential node and finds its base name and an identifier if
 * present. Also classifies it according to ENameType.
 *
 * The identifier is a number between parentheses placed at the end of the name. There can be a
 * space before it or not.
 * 0 is a valid identifier.
 *
 * Examples:
 *     - "test"      -> {"test", baseNameOnly, 0}
 *     - "test (1)"  -> {"test", withIdSpace, 1}
 *     - "test (0)"  -> {"test (0)", withIdSpace, 0}
 *     - "test(2)"   -> {"test(2)", withIdNoSpace, 0}
 *     - "test  (3)" -> {"test ", withIdSpace, 3}
 *
 * @param input The name to split
 * @return a tuple with [base name, the type of the input name, id]
 */
std::tuple<std::string, ENameType, nameId_t> getBaseNameKindId(const std::string& input);

/**
 * @brief Returns the index where the last '.' can be found in the fileName
 *
 * If there is not '.' in the input string, fileName.size() is returned
 *
 * @note This index is intended to be used with std::string::substr like:
 * size_t dotPos = fileExtensionDotPosition(fileName);
 * std::stirng basename = fileName.substr(0, dotPos);
 * std::stirng extension = fileName.substr(dotPos); // It will contain the '.' if present
 */
size_t fileExtensionDotPosition(const std::string& fileName);

/**
 * @class NewFreeIndexProvider
 * @brief Helper class to keep track of the indices that are being used and also to retrieve from it
 * the next available index that should be used.
 *
 * The class manages two independent list of indices, the ones for names that fall into the
 * ENameType::withIdNoSpace and the ones for ENameType::withIdSpace. Also it tracks if the raw
 * base name is occupied.
 */
class NewFreeIndexProvider
{
public:
    /**
     * @brief Updates the internal occupied information
     * @note if kind is ENameType::baseNameOnly the index is not used
     */
    void addOccupiedIndex(const ENameType kind, const nameId_t index);

    /**
     * @brief Returns the next available index and adds it to the list of occupied states
     *
     * This will return the lowest index that is not in the list of occupied but always greater or
     * equal to the given minimumAllowed
     */
    nameId_t getNextFreeIndex(const ENameType kind, nameId_t minimumAllowed);

    /**
     * @brief Returns true if the index is not in the internal occupied information
     */
    bool isFree(const ENameType kind, const nameId_t index) const;

private:
    /**
     * @brief In the following lines members to keep track of the occupied state are defined.
     * Let's keep it simple for now and store all the occupied indices.
     * This could be optimized in the future if needed, for example, by storing occupied ranges
     * instead of individual indices.
     *
     * @note The elements in the vectors are sorted from lowest to highest
     */
    std::vector<nameId_t> occupiedIndicesNoSpace;
    std::vector<nameId_t> occupiedIndicesSpace;
    bool baseNameOccupied{false};
};

/**
 * @class NameCollisionSolver
 * @brief A class to solve name clashing issues.
 *
 * The use case for this class is the following scenario:
 *     - You have a location (destiny) where you want to put new elements (source).
 *     - The elements have names.
 *     - There can not be elements with the same name in the destiny.
 *     - You need to process one by one the elements in source to know which is the name it must
 *     have in the destiny to avoid name collisions.
 *
 * Usage example:
 *     1. Create an object. You can create it with a list of already existing names in a target
 *     destiny.
 *     2. Iterate through the elements in you source list and call the operator() to obtain the
 *     fixed name.
 *
 * Name resolution logic:
 *     - If source name has no "(N)" in it, " (N)" is appended to the name. N is the lowest number
 *     available (starting from 1) that avoids collision.
 *     - If source name has "(N)" in it, that N gets increased until a free number is found.
 *
 */
class NameCollisionSolver
{
public:
    NameCollisionSolver() = default;

    NameCollisionSolver(const std::vector<std::string>& existingNames);

    /**
     * @brief Returns the name that the element "nameToValidate" must have to avoid name collisions
     * in the destiny.
     * @note This is not a const method, i.e. it changes the internal state of the object. Be
     * careful calling this operator twice with the same source name.
     */
    std::string operator()(const std::string& nameToValidate);

private:
    /**
     * @brief For each base name (name without "(N)") we store an index provider
     */
    std::map<std::string, NewFreeIndexProvider> indexProviders;
};

/**
 * @class FileNameCollisionSolver
 * @brief Same idea as NameCollisionSolver but to operate on names that may have an extension (such
 * as file names).
 *
 * The difference is that instead of adding " (N)" at the end of the name, it is added before the
 * extension (if it exists). Example: test.txt -> test (1).txt
 *
 * See NameCollisionSolver documentation for additional information
 *
 * @note This class shares the interface with NameCollisionSolver but this interface is not defined
 * to avoid vtable lookups. If you find it useful, consider defining it.
 */
class FileNameCollisionSolver
{
public:
    FileNameCollisionSolver() = default;

    FileNameCollisionSolver(const std::vector<std::string>& existingNames);

    std::string operator()(const std::string& nameToValidate);

private:
    std::map<std::string, NewFreeIndexProvider> indexProviders;
};

}

#endif
