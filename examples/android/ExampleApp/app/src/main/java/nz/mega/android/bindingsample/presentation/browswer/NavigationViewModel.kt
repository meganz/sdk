/**
 * NavigationViewModel.kt
 * ViewModel for NavigationScreen to manage file browser state and business logic
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
package nz.mega.android.bindingsample.presentation.browswer

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import nz.mega.android.bindingsample.R
import nz.mega.android.bindingsample.data.LogoutResult
import nz.mega.android.bindingsample.data.SdkRepository
import nz.mega.sdk.MegaNode

class NavigationViewModel(application: Application) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<NavigationUiState>(NavigationUiState.Initial)
    val uiState: StateFlow<NavigationUiState> = _uiState.asStateFlow()

    /**
     * Load nodes for a given parent handle.
     * If parentHandle is -1, loads root node.
     */
    fun loadNodes(parentHandle: Long = -1) {
        viewModelScope.launch {
            _uiState.value = NavigationUiState.Loading

            try {
                val parentNode = if (parentHandle == -1L) {
                    SdkRepository.getRootNode()
                } else {
                    SdkRepository.getNodeByHandle(parentHandle)
                }

                if (parentNode == null) {
                    _uiState.value = NavigationUiState.Error(
                        errorMessage = "Failed to load parent node",
                        parentHandle = parentHandle
                    )
                    return@launch
                }

                val children = SdkRepository.getChildren(parentNode)
                val parentName = if (parentNode.type == MegaNode.TYPE_ROOT) {
                    getApplication<Application>().getString(R.string.cloud_drive)
                } else {
                    parentNode.name
                }

                if (children.isNullOrEmpty()) {
                    _uiState.value = NavigationUiState.Empty(
                        parentHandle = parentNode.handle,
                        parentName = parentName
                    )
                } else {
                    // Compute folder info for each folder node
                    val nodesWithInfo = children.map { node ->
                        if (node.isFolder) {
                            val folderChildren = SdkRepository.getChildren(node)
                            val folderInfo = formatFolderInfo(folderChildren, getApplication())
                            NodeDisplayInfo(node, folderInfo)
                        } else {
                            NodeDisplayInfo(node)
                        }
                    }
                    _uiState.value = NavigationUiState.Content(
                        nodes = nodesWithInfo,
                        parentHandle = parentNode.handle,
                        parentName = parentName
                    )
                }
            } catch (e: Exception) {
                _uiState.value = NavigationUiState.Error(
                    errorMessage = e.message ?: "Unknown error occurred",
                    parentHandle = parentHandle
                )
            }
        }
    }

    /**
     * Navigate into a folder node.
     */
    fun navigateToFolder(node: MegaNode) {
        if (node.isFolder) {
            loadNodes(node.handle)
        }
    }

    /**
     * Navigate to parent folder.
     */
    fun navigateUp() {
        val currentState = _uiState.value
        val currentParentHandle = when (currentState) {
            is NavigationUiState.Content -> currentState.parentHandle
            is NavigationUiState.Empty -> currentState.parentHandle
            else -> -1L
        }

        if (currentParentHandle == -1L) {
            // Already at root, cannot navigate up
            return
        }

        viewModelScope.launch {
            try {
                val currentNode = SdkRepository.getNodeByHandle(currentParentHandle)
                if (currentNode == null) {
                    // Invalid state, load root
                    loadNodes(-1)
                    return@launch
                }

                // Check if current node is root
                if (currentNode.type == MegaNode.TYPE_ROOT) {
                    // Already at root, cannot navigate up
                    return@launch
                }

                val parentNode = SdkRepository.getParentNode(currentNode)

                if (parentNode != null) {
                    loadNodes(parentNode.handle)
                } else {
                    // No parent means we're at root, load root
                    loadNodes(-1)
                }
            } catch (e: Exception) {
                _uiState.value = NavigationUiState.Error(
                    errorMessage = e.message ?: "Failed to navigate up",
                    parentHandle = currentParentHandle
                )
            }
        }
    }

    /**
     * Initiate logout.
     */
    fun logout() {
        _uiState.value = NavigationUiState.LoggingOut

        viewModelScope.launch {
            SdkRepository.logout().collect { result ->
                when (result) {
                    is LogoutResult.LogoutSuccess -> {
                        _uiState.value = NavigationUiState.LogoutSuccess
                    }
                    is LogoutResult.LogoutFailure -> {
                        val currentState = _uiState.value
                        val parentHandle = when (currentState) {
                            is NavigationUiState.Content -> currentState.parentHandle
                            is NavigationUiState.Empty -> currentState.parentHandle
                            else -> -1L
                        }
                        val parentName = when (currentState) {
                            is NavigationUiState.Content -> currentState.parentName
                            is NavigationUiState.Empty -> currentState.parentName
                            else -> ""
                        }
                        _uiState.value = NavigationUiState.Error(
                            errorMessage = result.errorMessage,
                            parentHandle = parentHandle,
                            parentName = parentName
                        )
                    }
                }
            }
        }
    }

    /**
     * Handle saved state restoration.
     * Should be called from Activity onCreate with saved state.
     */
    fun handleSavedState(parentHandle: Long?) {
        if (_uiState.value is NavigationUiState.Initial) {
            loadNodes(parentHandle ?: -1)
        }
    }

    /**
     * Format folder info string (number of folders and files)
     */
    private fun formatFolderInfo(children: ArrayList<MegaNode>?, context: Context): String {
        if (children == null) return "0 files"

        var numFolders = 0
        var numFiles = 0

        for (c in children) {
            if (c.isFolder) {
                numFolders++
            } else {
                numFiles++
            }
        }

        return when {
            numFolders > 0 && numFiles > 0 -> {
                val folderPlural = context.resources.getQuantityString(
                    R.plurals.general_num_folders,
                    numFolders
                )
                val filePlural = context.resources.getQuantityString(
                    R.plurals.general_num_files,
                    numFiles
                )
                "$numFolders $folderPlural, $numFiles $filePlural"
            }
            numFolders > 0 -> {
                val folderPlural = context.resources.getQuantityString(
                    R.plurals.general_num_folders,
                    numFolders
                )
                "$numFolders $folderPlural"
            }
            numFiles > 0 -> {
                val filePlural = context.resources.getQuantityString(
                    R.plurals.general_num_files,
                    numFiles
                )
                "$numFiles $filePlural"
            }
            else -> "Empty"
        }
    }
}

