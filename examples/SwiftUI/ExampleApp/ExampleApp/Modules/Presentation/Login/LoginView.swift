import SwiftUI
import MEGADesignToken

struct LoginView: View {
    @Bindable var viewModel: LoginViewModel
    @State private var showNodes = false
    
    var body: some View {
        NavigationStack {
            VStack {
                Spacer()
                Form(content: {
                    TextField("Email", text: $viewModel.email, prompt: Text("Email"))
                        .disableAutocorrection(true)
                    #if os(iOS)
                        .autocapitalization(.none)
                        .keyboardType(.emailAddress)
                    #endif
                    SecureField("Password", text: $viewModel.password, prompt: Text("Password"))
                })
                Spacer()
                Text(viewModel.message)
                    .opacity(viewModel.message.isEmpty ? 0.0 : 1.0)
                Spacer()
                Button("Login", action: {
                    viewModel.message = "Log in..."
                    Task {
                        do {
                            try await viewModel.login()
                            try await viewModel.fetchNodes()
                            showNodes = true
                        } catch {
                            viewModel.message = "Invalid email or password"
                        }
                    }
                })
            }
            .navigationDestination(isPresented: $showNodes) {
                NodeListView()
            }
            .padding(30)
            .background(TokenColors.Background.page.swiftUI)

        }
    }
}

#Preview {
    LoginView(viewModel: Dependency.loginViewModel)
}
