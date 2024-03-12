import Foundation
import MEGADomain

@Observable class NodeListViewModel {
    private var nodeUseCase: any NodeUseCaseProtocol
    var nodes: [NodeEntity] = []
    
    init(nodeUseCase: some NodeUseCaseProtocol) {
        self.nodeUseCase = nodeUseCase
    }
    
    func childrenForRootNode() {
        nodes = nodeUseCase.childrenForRoot()
    }
}
