import SwiftUI
import MEGADomain

struct NodeView: View {
    var viewModel: NodeViewModel
    
    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text(viewModel.name)
                    .font(.headline)
                Text(viewModel.creationString)
                    .font(.subheadline)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}
