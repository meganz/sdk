import Foundation
import MEGADomain
import MEGASdk

public struct AuthRepository: AuthRepositoryProtocol {
    
    public static var newRepo: AuthRepository {
        AuthRepository(sdk: MEGASdk.sharedSdk)
    }
    
    private let sdk: MEGASdk
    
    public init(sdk: MEGASdk) {
        self.sdk = sdk
    }
    
    public func login(email: String, password: String) async throws {
        try await withCheckedThrowingContinuation { continuation in
            sdk.login(withEmail: email, password: password, delegate: RequestDelegate { result in
                switch result {
                case .success:
                    continuation.resume()
                case .failure(let error):
                    continuation.resume(throwing: error)
                }
            })
        }
    }
    
    public func fetchNodes() async throws {
        try await withCheckedThrowingContinuation { continuation in
            sdk.fetchNodes(with: RequestDelegate { result in
                switch result {
                case .success:
                    continuation.resume()
                case .failure(let error):
                    continuation.resume(throwing: error)
                }
            })
        }
    }
}
