import Foundation
import MEGADomain
import MEGASdk

public struct NodeRepository: NodeRepositoryProtocol {
    public static var newRepo: NodeRepository {
        NodeRepository(sdk: MEGASdk.sharedSdk)
    }
    
    private let sdk: MEGASdk
    
    public init(sdk: MEGASdk) {
        self.sdk = sdk
    }
    
    public func childrenForRoot() -> [NodeEntity] {
        guard let parent = sdk.rootNode else { return [] }
        let nodeList = sdk.children(forParent: parent)
        return nodeList.toNodeEntities()
    }
}
