/**
 * LoginUiState.kt
 * UI state data class for login screen
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
package nz.mega.android.bindingsample.presentation.login

/**
 * Sealed interface representing different states of the login UI.
 * Each state represents a specific phase of the login process.
 */
sealed interface LoginUiState {
    val email: String?
    val password: String?

    /**
     * Initial state - user can input credentials.
     * Form fields are visible and editable.
     */
    data class Initial(
        override val email: String = "",
        override val password: String = "",
        val emailError: String? = null,
        val passwordError: String? = null
    ) : LoginUiState

    /**
     * Logging in state - login request is in progress.
     * Form fields are hidden, loading indicator is shown.
     */
    data class LoggingIn(
        override val email: String,
        override val password: String
    ) : LoginUiState

    /**
     * Fetching nodes state - nodes are being fetched after successful login.
     * Progress bar is visible.
     * @param progress Progress value between 0.0f and 1.0f
     */
    data class FetchingNodes(
        override val email: String = "",
        override val password: String = "",
        val progress: Float = 0.0f
    ) : LoginUiState

    /**
     * Error state - login or network error occurred.
     * Form fields are visible with error message displayed.
     */
    data class Error(
        override val email: String = "",
        override val password: String = "",
        val errorMessage: String
    ) : LoginUiState

    /**
     * Success state - login successful.
     * Navigation should be triggered from the Composable.
     */
    data class Success(
        override val email: String = "",
        override val password: String = "",
    ) : LoginUiState
}

