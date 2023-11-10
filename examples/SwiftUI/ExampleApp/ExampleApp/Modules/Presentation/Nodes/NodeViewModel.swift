import Foundation
import MEGADomain

@Observable class NodeViewModel {
    private var node: NodeEntity
    var name: String
    var creationString: String
    
    init(node: NodeEntity) {
        self.node = node
        self.name = node.name
        self.creationString = node.creationTime.description
    }
}
