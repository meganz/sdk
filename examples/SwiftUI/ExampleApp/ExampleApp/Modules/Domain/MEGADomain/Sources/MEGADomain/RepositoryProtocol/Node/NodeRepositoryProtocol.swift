import Foundation

public protocol NodeRepositoryProtocol: RepositoryProtocol {
    func childrenForRoot() -> [NodeEntity]
}
