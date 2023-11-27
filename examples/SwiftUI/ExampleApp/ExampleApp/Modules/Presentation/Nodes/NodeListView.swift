
import SwiftUI

struct NodeListView: View {
    var viewModel = Dependency.nodeListViewModel
    var body: some View {
        NavigationStack {
            if viewModel.nodes.isEmpty {
                Spacer()
                Button("Load nodes", action: {
                    viewModel.childrenForRootNode()
                })
                Spacer()
            } else {
                List(viewModel.nodes) { node in
                    NodeView(viewModel: NodeViewModel(node: node))
                }
            }
        }
        .navigationTitle("Cloud Drive")
        .navigationBarBackButtonHidden()
    }
}

#Preview {
    NodeListView()
}
