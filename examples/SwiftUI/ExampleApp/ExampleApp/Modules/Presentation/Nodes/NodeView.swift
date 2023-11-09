import SwiftUI
import MEGADomain

struct NodeView: View {
    var viewModel: NodeViewModel
    
    var body: some View {
        VStack(alignment: .leading) {
            Text(viewModel.name)
                .font(.headline)
            Text(viewModel.creationString)
                .font(.subheadline)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

#Preview {
    NodeView(viewModel: NodeViewModel(node: NodeEntity(name: "example",
                                                       handle: 1,
                                                       size: 1024,
                                                       creationTime: Date.now,
                                                       modificationTime: .now)))
}
