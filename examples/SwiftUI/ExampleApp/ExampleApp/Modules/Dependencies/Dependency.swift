import MEGADomain
import MEGASdkRepo

enum Dependency {
    static var loginViewModel: LoginViewModel {
        LoginViewModel(authUseCase: AuthUseCase(repo: AuthRepository.newRepo))
    }
    static var nodeListViewModel: NodeListViewModel {
        NodeListViewModel(nodeUseCase: NodeUseCase(repo: NodeRepository.newRepo))
    }
}
