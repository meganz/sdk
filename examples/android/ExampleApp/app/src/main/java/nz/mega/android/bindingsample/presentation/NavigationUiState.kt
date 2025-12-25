/**
 * NavigationUiState.kt
 * UI state data class for navigation screen
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
package nz.mega.android.bindingsample.presentation

import nz.mega.sdk.MegaNode

/**
 * Data class to hold node with its display information
 */
data class NodeDisplayInfo(
    val node: MegaNode,
    val folderInfo: String? = null // null for files, computed info for folders
)

/**
 * Sealed interface representing different states of the navigation UI.
 * Each state represents a specific phase of the file browser.
 */
sealed interface NavigationUiState {
    /**
     * Initial state - initializing navigation
     */
    data object Initial : NavigationUiState

    /**
     * Loading state - loading nodes for current folder
     */
    data object Loading : NavigationUiState

    /**
     * Content state - displaying nodes with current parent info
     * @param nodes List of child nodes with display info
     * @param parentHandle Handle of the current parent node
     * @param parentName Name of the current parent node
     */
    data class Content(
        val nodes: List<NodeDisplayInfo>,
        val parentHandle: Long,
        val parentName: String
    ) : NavigationUiState

    /**
     * Empty state - current folder is empty
     * @param parentHandle Handle of the current parent node
     * @param parentName Name of the current parent node
     */
    data class Empty(
        val parentHandle: Long,
        val parentName: String
    ) : NavigationUiState

    /**
     * Error state - error occurred while loading nodes
     * @param errorMessage Error message describing the failure
     * @param parentHandle Handle of the current parent node (if available)
     * @param parentName Name of the current parent node (if available)
     */
    data class Error(
        val errorMessage: String,
        val parentHandle: Long = -1,
        val parentName: String = ""
    ) : NavigationUiState

    /**
     * Logging out state - logout request is in progress
     */
    data object LoggingOut : NavigationUiState

    /**
     * Logout success state - logout completed successfully
     */
    data object LogoutSuccess : NavigationUiState
}

