#include "mega/name_collision.h"

#include "mega/utils.h"

#include <functional>
#include <regex>

namespace mega::ncoll
{

std::tuple<std::string, ENameType, nameId_t> getBaseNameKindId(const std::string& input)
{
    static const std::regex pattern(R"(^(.*?)(\ ?)\((\d+)\)$)");

    if (std::smatch matches; std::regex_match(input, matches, pattern) && matches[1].matched &&
                             matches[2].matched && matches[3].matched)
    {
        const nameId_t identifier = static_cast<nameId_t>(std::stoul(matches[3].str()));
        const ENameType t =
            matches[2].str().empty() ? ENameType::withIdNoSpace : ENameType::withIdSpace;
        return {matches[1].str(), t, identifier};
    }
    return {input, ENameType::baseNameOnly, 0};
}

void NewFreeIndexProvider::addOccupiedIndex(const ENameType kind, const nameId_t index)
{
    static const auto addIndex = [](const nameId_t index, std::vector<nameId_t>& containter)
    {
        auto it = std::lower_bound(std::cbegin(containter), std::cend(containter), index);
        containter.insert(it, index);
    };
    switch (kind)
    {
        case ENameType::baseNameOnly:
            baseNameOccupied = true;
            return;
        case ENameType::withIdSpace:
            addIndex(index, occupiedIndicesSpace);
            return;
        case ENameType::withIdNoSpace:
            addIndex(index, occupiedIndicesNoSpace);
            return;
    }
}

nameId_t NewFreeIndexProvider::getNextFreeIndex(ENameType kind, nameId_t minimumAllowed)
{
    if (kind == ENameType::baseNameOnly)
    {
        if (!baseNameOccupied)
        {
            baseNameOccupied = true;
            return 0;
        }
        kind = ENameType::withIdSpace;
        minimumAllowed = 1;
    }
    auto& container =
        kind == ENameType::withIdSpace ? occupiedIndicesSpace : occupiedIndicesNoSpace;

    auto startLookIt =
        std::lower_bound(std::cbegin(container), std::cend(container), minimumAllowed);
    if (startLookIt == std::end(container) || *startLookIt != minimumAllowed)
        return *container.insert(startLookIt, minimumAllowed);

    // Increase minimumAllowed until a free index is found.
    while (startLookIt != std::end(container))
    {
        if (*startLookIt != minimumAllowed)
            break;
        ++minimumAllowed;
        ++startLookIt;
    }
    return *container.insert(startLookIt, minimumAllowed);
}

bool NewFreeIndexProvider::isFree(const ENameType kind, const nameId_t index) const
{
    if (kind == ENameType::baseNameOnly)
        return !baseNameOccupied;
    const auto& container =
        kind == ENameType::withIdSpace ? occupiedIndicesSpace : occupiedIndicesNoSpace;
    const auto it = std::lower_bound(std::cbegin(container), std::cend(container), index);
    return it == std::cend(container) || *it != index;
}

NameCollisionSolver::NameCollisionSolver(const std::vector<std::string>& existingNames)
{
    std::for_each(std::begin(existingNames),
                  std::end(existingNames),
                  [this](const std::string& name)
                  {
                      const auto [basename, kind, id] = getBaseNameKindId(name);
                      indexProviders[basename].addOccupiedIndex(kind, id);
                  });
}

std::string NameCollisionSolver::operator()(const std::string& nameToValidate)
{
    const auto [basename, kind, id] = getBaseNameKindId(nameToValidate);
    auto& provider = indexProviders[basename];
    if (provider.isFree(kind, id))
    {
        provider.addOccupiedIndex(kind, id);
        return nameToValidate;
    }
    nameId_t validId = provider.getNextFreeIndex(kind, id);
    std::string result = basename;
    if (kind == ENameType::withIdSpace || kind == ENameType::baseNameOnly)
        result += ' ';
    return result + "(" + std::to_string(validId) + ")";
}

FileNameCollisionSolver::FileNameCollisionSolver(const std::vector<std::string>& existingNames)
{
    std::for_each(std::begin(existingNames),
                  std::end(existingNames),
                  [this](const std::string& name)
                  {
                      const size_t dotPos = fileExtensionDotPosition(name);
                      const auto [basename, kind, id] = getBaseNameKindId(name.substr(0, dotPos));
                      indexProviders[basename + name.substr(dotPos)].addOccupiedIndex(kind, id);
                  });
}

std::string FileNameCollisionSolver::operator()(const std::string& nameToValidate)
{
    const size_t dotPos = fileExtensionDotPosition(nameToValidate);
    const auto [basename, kind, id] = getBaseNameKindId(nameToValidate.substr(0, dotPos));
    const std::string extension = nameToValidate.substr(dotPos);
    auto& provider = indexProviders[basename + extension];
    if (provider.isFree(kind, id))
    {
        provider.addOccupiedIndex(kind, id);
        return nameToValidate;
    }
    nameId_t validId = provider.getNextFreeIndex(kind, id);
    std::string result = basename;
    if (kind == ENameType::withIdSpace || kind == ENameType::baseNameOnly)
        result += ' ';
    return result + "(" + std::to_string(validId) + ")" + extension;
}
}
