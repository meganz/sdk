#pragma once

#include <mega/file_service/avl_tree.h>

#include <cinttypes>
#include <fstream>
#include <string>

namespace mega
{
namespace file_service
{

template<typename Traits>
std::ostream& render(std::ostream& ostream, const AVLTree<Traits>& tree)
{
    ostream << "digraph {\n";

    render(tree.root(), ostream, tree);

    ostream << "}\n";

    return ostream;
}

template<typename Traits>
void render(const std::string& path, const AVLTree<Traits>& tree)
{
    std::ofstream ostream(path, std::ios::trunc);

    render(ostream, tree);
}

template<typename Traits>
std::ostream& render(typename AVLTree<Traits>::ConstIterator iterator,
                     std::ostream& ostream,
                     const AVLTree<Traits>& tree)
{
    // No node? Nothing to render.
    if (!iterator)
        return ostream;

    // Generate a unique ID for the node referenced by iterator.
    auto id = [](auto iterator)
    {
        return reinterpret_cast<std::uintmax_t>(&*iterator);
    }; // id

    // Render a child.
    auto renderChild = [&id, &ostream, &tree](auto iterator)
    {
        // No child? Nothing to render.
        if (!iterator)
            return;

        // Render the child.
        render(iterator, ostream, tree);

        // Get our hands on the child's parent.
        auto parent = iterator.parent();

        // Assume child is parent's left child.
        auto port = "sw";

        // Child is actually parent's right child.
        if (iterator == parent.right())
            port = "se";

        // Link the parent to its child.
        ostream << id(parent) << ":" << port << " -> " << id(iterator) << ";\n";

        // Link the child to its parent.
        ostream << id(iterator) << ":n"
                << " -> " << id(parent) << ":" << port << ";\n";
    }; // renderChild

    // Convenience.
    using KeyTraits = typename AVLTree<Traits>::KeyTraits;

    // Render this node.
    ostream << id(iterator) << " [ label = \"" << KeyTraits::key(*iterator) << "\" ];\n";

    // Render this node's left and right children.
    renderChild(iterator.left());
    renderChild(iterator.right());

    // Return iterator to caller.
    return ostream;
}

} // file_service
} // mega
