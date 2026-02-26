import Foundation
import MEGADomain

@Observable class LoginViewModel {
    private var authUseCase: any AuthUseCaseProtocol
    var email: String = ""
    var password: String = ""
    var message: String = ""
    
    init(authUseCase: some AuthUseCaseProtocol) {
        self.authUseCase = authUseCase
    }
    
    func login() async throws {
        try await authUseCase.login(email: email, password: password)
    }
    
    func fetchNodes() async throws {
        try await authUseCase.fetchNodes()
    }
}
