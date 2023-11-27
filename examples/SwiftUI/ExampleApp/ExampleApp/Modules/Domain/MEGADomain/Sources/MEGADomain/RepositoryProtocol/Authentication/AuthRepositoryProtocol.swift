import Foundation

public protocol AuthRepositoryProtocol: RepositoryProtocol {
    func login(email: String, password: String) async throws
    func fetchNodes() async throws
}
