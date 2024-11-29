import Foundation
import MEGADomain
import MEGASdk

extension NodeEntity {
    public func toMEGANode(in sdk: MEGASdk) -> MEGANode? {
        sdk.node(forHandle: handle)
    }
}

extension Array where Element == NodeEntity {
    public func toMEGANodes(in sdk: MEGASdk) -> [MEGANode] {
        compactMap { $0.toMEGANode(in: sdk) }
    }
}

extension Array where Element: MEGANode {
    public func toNodeEntities() -> [NodeEntity] {
        map { $0.toNodeEntity() }
    }
}

extension MEGANode {
    public func toNodeEntity() -> NodeEntity {
        NodeEntity(node: self)
    }
}

fileprivate extension NodeEntity {
    init(node: MEGANode) {
        self.init(
            name : node.name ?? "",
            handle : node.handle,
            size : node.size?.uint64Value ?? 0,
            creationTime : node.creationTime ?? Date(),
            modificationTime : node.modificationTime ?? Date()
        )
    }
}
