import MEGADomain
import MEGASdk

extension MEGANodeList {    
    public func toNodeEntities() -> [NodeEntity] {
        guard size > 0 else { return [] }
        return (0..<size).compactMap { node(at: $0)?.toNodeEntity() }
    }
}
