import MEGADomain
import MEGASdk

public extension MEGASdk {
    
    /// MEGASdk instance used for the user logged account
    static let sharedSdk: MEGASdk = {
        MEGASdk.setLogLevel(.max)
        MEGASdk.setLogToConsole(true)
        let baseURL: URL? = try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        let sdk = MEGASdk(appKey:"", userAgent: nil, basePath: baseURL?.path)
        sdk?.setRLimitFileCount(20000)
        sdk?.retrySSLErrors(true)
        guard let sdk else {
            fatalError("Can't create shared sdk")
        }
        return sdk
    }()
}
