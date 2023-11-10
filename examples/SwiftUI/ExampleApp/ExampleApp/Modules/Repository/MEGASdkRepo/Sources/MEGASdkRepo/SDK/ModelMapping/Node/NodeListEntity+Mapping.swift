import MEGADomain
import MEGASdk

extension MEGANodeList {    
    public func toNodeEntities() -> [NodeEntity] {
        guard (size?.intValue ?? 0) > 0 else { return [] }
        return (0..<size.intValue).compactMap { node(at: $0).toNodeEntity() }
    }
}
