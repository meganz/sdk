/**
 * SdkRepository.kt
 * Repository class for MEGA SDK operations
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
package nz.mega.android.bindingsample.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.withContext
import nz.mega.sdk.MegaApiAndroid
import nz.mega.sdk.MegaApiJava
import nz.mega.sdk.MegaError
import nz.mega.sdk.MegaNode
import nz.mega.sdk.MegaRequest
import nz.mega.sdk.MegaRequestListenerInterface

/**
 * Sealed interface for login operation results
 */
sealed interface LoginResult {
    /**
     * Login successful result
     */
    data object LoginSuccess : LoginResult

    /**
     * Login failure result
     * @param errorMessage Error message describing the failure
     */
    data class LoginFailure(val errorMessage: String) : LoginResult
}


/**
 * Sealed interface for fetch nodes operation results
 */
sealed interface FetchNodesResult {
    /**
     * Fetch nodes operation started
     */
    data object Started : FetchNodesResult

    /**
     * Fetch nodes operation in progress
     * @param progressValue Progress value between 0.0 and 1.0
     */
    data class Progressing(val progressValue: Float) : FetchNodesResult

    /**
     * Fetch nodes operation completed successfully
     */
    data object Completed : FetchNodesResult

    /**
     * Fetch nodes operation failed
     * @param errorMessage Error message describing the failure
     */
    data class Error(val errorMessage: String) : FetchNodesResult
}


/**
 * Singleton repository class that provides a clean interface to MEGA SDK operations.
 * This abstracts the SDK implementation details from the ViewModel layer.
 */
object SdkRepository {
    @Volatile
    private var megaApi: MegaApiAndroid? = null

    /**
     * Initialize the repository with MegaApiAndroid instance
     * Must be called before using the repository
     */
    fun initialize(megaApi: MegaApiAndroid) {
        this.megaApi = megaApi
    }

    /**
     * Login to MEGA account
     * @param email User email address
     * @param password User password
     * @return Flow of LoginResult (LoginSuccess or LoginFailure)
     *
     * This method ensures all SDK operations run on a worker thread (IO dispatcher)
     * to avoid blocking the main thread.
     */
    fun login(email: String, password: String): Flow<LoginResult> = callbackFlow {
        val api = megaApi ?: run {
            trySend(LoginResult.LoginFailure("MegaApi not initialized"))
            close()
            return@callbackFlow
        }

        val listener = object : MegaRequestListenerInterface {
            override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {
                // Login started - no action needed
            }

            override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {
                // Login doesn't have progress updates
            }

            override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
                if (request.type == MegaRequest.TYPE_LOGIN) {
                    if (e.errorCode == MegaError.API_OK) {
                        trySend(LoginResult.LoginSuccess)
                    } else {
                        val errorMessage = if (e.errorCode == MegaError.API_ENOENT) {
                            "Incorrect email or password"
                        } else {
                            e.errorString ?: "Login failed"
                        }
                        trySend(LoginResult.LoginFailure(errorMessage))
                    }
                    close()
                }
            }

            override fun onRequestTemporaryError(
                api: MegaApiJava,
                request: MegaRequest,
                e: MegaError
            ) {
                // Handle temporary errors if needed
            }
        }

        api.login(email, password, listener)

        awaitClose {
            // Cleanup if needed
        }
    }.flowOn(Dispatchers.IO)

    /**
     * Fetch nodes from MEGA account
     * @return Flow of FetchNodesResult (Started, Progressing, or Completed)
     *
     * This method ensures all SDK operations run on a worker thread (IO dispatcher)
     * to avoid blocking the main thread.
     */
    fun fetchNodes(): Flow<FetchNodesResult> = callbackFlow {
        val api = megaApi ?: run {
            close()
            return@callbackFlow
        }

        val listener = object : MegaRequestListenerInterface {
            override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {
                if (request.type == MegaRequest.TYPE_FETCH_NODES) {
                    trySend(FetchNodesResult.Started)
                }
            }

            override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {
                if (request.type == MegaRequest.TYPE_FETCH_NODES) {
                    if (request.totalBytes > 0) {
                        var progressValue =
                            request.transferredBytes.toFloat() / request.totalBytes.toFloat()
                        // Clamp progress between 0.0 and 1.0
                        progressValue = progressValue.coerceIn(0.0f, 1.0f)
                        trySend(FetchNodesResult.Progressing(progressValue))
                    }
                }
            }

            override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
                if (request.type == MegaRequest.TYPE_FETCH_NODES) {
                    if (e.errorCode == MegaError.API_OK) {
                        trySend(FetchNodesResult.Completed)
                    } else {
                        val errorMessage = e.errorString ?: "Failed to fetch nodes"
                        trySend(FetchNodesResult.Error(errorMessage))
                    }
                    close()
                }
            }

            override fun onRequestTemporaryError(
                api: MegaApiJava,
                request: MegaRequest,
                e: MegaError
            ) {
                // Handle temporary errors if needed
            }
        }

        api.fetchNodes(listener)

        awaitClose {
            // Cleanup if needed
        }
    }.flowOn(Dispatchers.IO)

    /**
     * Helper function to execute SDK operations on IO dispatcher.
     * Ensures the API is initialized before executing the operation.
     */
    private suspend fun <T> executeOnIo(operation: (MegaApiAndroid) -> T): T? = withContext(Dispatchers.IO) {
        val api = megaApi ?: return@withContext null
        operation(api)
    }

    /**
     * Get a node by its handle.
     * @param handle The handle of the node to retrieve
     * @return The MegaNode if found, null if not found or if API is not initialized
     *
     * This method runs on a worker thread (IO dispatcher) to avoid blocking the main thread.
     */
    suspend fun getNodeByHandle(handle: Long): MegaNode? = executeOnIo { api ->
        api.getNodeByHandle(handle)
    }

    /**
     * Get the root node of the MEGA account.
     * @return The root MegaNode, or null if not available or if API is not initialized
     *
     * This method runs on a worker thread (IO dispatcher) to avoid blocking the main thread.
     */
    suspend fun getRootNode(): MegaNode? = executeOnIo { api ->
        api.getRootNode()
    }

    /**
     * Get the children of a node.
     * @param node The parent node
     * @return List of child MegaNodes, or null if the node is null, not available, or if API is not initialized
     *
     * This method runs on a worker thread (IO dispatcher) to avoid blocking the main thread.
     */
    suspend fun getChildren(node: MegaNode?): ArrayList<MegaNode>? = executeOnIo { api ->
        node?.let { api.getChildren(it) }
    }

    /**
     * Get the parent node of a given node.
     * @param node The child node
     * @return The parent MegaNode, or null if the node is root, null, or if API is not initialized
     *
     * This method runs on a worker thread (IO dispatcher) to avoid blocking the main thread.
     */
    suspend fun getParentNode(node: MegaNode?): MegaNode? = executeOnIo { api ->
        node?.let { api.getParentNode(it) }
    }
}
