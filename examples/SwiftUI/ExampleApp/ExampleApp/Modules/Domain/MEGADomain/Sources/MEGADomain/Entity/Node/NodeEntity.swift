import Foundation

public typealias HandleEntity = UInt64

public struct NodeEntity: Sendable {
    public let name: String
    public let handle: HandleEntity
    public let size: UInt64
    public let creationTime: Date
    public let modificationTime: Date
    
    public init(name: String, 
                handle: HandleEntity,
                size: UInt64,
                creationTime: Date,
                modificationTime: Date) {
        self.name = name
        self.handle = handle
        self.size = size
        self.creationTime = creationTime
        self.modificationTime = modificationTime
    }
}

extension NodeEntity: Identifiable {
    public var id: HandleEntity { handle }
}
