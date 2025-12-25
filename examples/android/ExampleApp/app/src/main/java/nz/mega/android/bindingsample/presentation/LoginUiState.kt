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
package nz.mega.android.bindingsample.presentation

const val DEFAULT_EMAIL = "rsh+21@mega.co.nz"

/**
 * Sealed interface representing different states of the login UI.
 * Each state represents a specific phase of the login process.
 */
sealed interface LoginUiState {
    /**
     * Initial state - user can input credentials.
     * Form fields are visible and editable.
     */
    data class Initial(
        val email: String = DEFAULT_EMAIL,
        val password: String = "",
        val emailError: String? = null,
        val passwordError: String? = null
    ) : LoginUiState

    /**
     * Logging in state - login request is in progress.
     * Form fields are hidden, loading indicator is shown.
     */
    data class LoggingIn(
        val email: String,
        val password: String
    ) : LoginUiState

    /**
     * Fetching nodes state - nodes are being fetched after successful login.
     * Progress bar is visible.
     */
    data class FetchingNodes(
        val email: String
    ) : LoginUiState

    /**
     * Error state - login or network error occurred.
     * Form fields are visible with error message displayed.
     */
    data class Error(
        val email: String,
        val password: String,
        val errorMessage: String
    ) : LoginUiState

    /**
     * Success state - login successful.
     * Navigation should be triggered from the Composable.
     */
    data class Success(
        val email: String
    ) : LoginUiState
}

