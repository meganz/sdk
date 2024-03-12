import Foundation

public protocol NodeUseCaseProtocol {
    func childrenForRoot() -> [NodeEntity]
}

public struct NodeUseCase: NodeUseCaseProtocol {
    
    private let repo: any NodeRepositoryProtocol
    
    public init(repo: some NodeRepositoryProtocol) {
        self.repo = repo
    }
    
    public func childrenForRoot() -> [NodeEntity] {
        repo.childrenForRoot()
    }
}
