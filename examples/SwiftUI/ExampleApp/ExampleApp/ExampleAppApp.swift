
import SwiftUI

@main
struct ExampleAppApp: App {
    var body: some Scene {
        WindowGroup {
            LoginView(viewModel: Dependency.loginViewModel)
        }
    }
}
