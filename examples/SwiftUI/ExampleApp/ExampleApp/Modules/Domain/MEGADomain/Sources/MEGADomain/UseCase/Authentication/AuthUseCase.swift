import Foundation

public protocol AuthUseCaseProtocol {
    func login(email: String, password: String) async throws
    func fetchNodes() async throws
}

public struct AuthUseCase: AuthUseCaseProtocol {
    private let repo: any AuthRepositoryProtocol
    
    public init(repo: some AuthRepositoryProtocol) {
        self.repo = repo
    }
    
    public func login(email: String, password: String) async throws {
        try await repo.login(email: email, password: password)
    }
    
    public func fetchNodes() async throws {
        try await repo.fetchNodes()
    }
}
