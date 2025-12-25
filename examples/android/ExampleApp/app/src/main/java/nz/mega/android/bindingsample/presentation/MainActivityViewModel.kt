/**
 * MainActivityViewModel.kt
 * ViewModel for MainActivity2 to manage login state and business logic
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

import android.app.Application
import android.util.Patterns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import nz.mega.android.bindingsample.R
import nz.mega.sdk.MegaApiAndroid

class MainActivityViewModel(application: Application) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(LoginUiState())
    val uiState: StateFlow<LoginUiState> = _uiState.asStateFlow()

    private var megaApi: MegaApiAndroid? = null

    init {
        val app = application as? DemoAndroidApplication
        megaApi = app?.getMegaApi()
    }

    fun updateEmail(email: String) {
        _uiState.value = _uiState.value.copy(
            email = email,
            emailError = null
        )
    }

    fun updatePassword(password: String) {
        _uiState.value = _uiState.value.copy(
            password = password,
            passwordError = null
        )
    }

    fun setShowProgressBar(show: Boolean) {
        _uiState.value = _uiState.value.copy(showProgressBar = show)
    }

    fun setShowFormFields(show: Boolean) {
        _uiState.value = _uiState.value.copy(showFormFields = show)
    }

    fun setLoading(loading: Boolean) {
        _uiState.value = _uiState.value.copy(isLoading = loading)
    }

    fun validateEmail(email: String): String? {
        val context = getApplication<Application>()
        return when {
            email.isEmpty() -> context.getString(R.string.error_enter_email)
            !Patterns.EMAIL_ADDRESS.matcher(email).matches() ->
                context.getString(R.string.error_invalid_email)
            else -> null
        }
    }

    fun validatePassword(password: String): String? {
        val context = getApplication<Application>()
        return if (password.isEmpty()) {
            context.getString(R.string.error_enter_password)
        } else {
            null
        }
    }

    fun validateForm(): Boolean {
        val currentState = _uiState.value
        val emailError = validateEmail(currentState.email)
        val passwordError = validatePassword(currentState.password)

        _uiState.value = currentState.copy(
            emailError = emailError,
            passwordError = passwordError
        )

        return emailError == null && passwordError == null
    }

    fun onLoginClick(onLoginSuccess: () -> Unit = {}) {
        if (!validateForm()) {
            return
        }

        val currentState = _uiState.value
        val email = currentState.email.lowercase().trim()
        val password = currentState.password

        // Update UI state for loading
        _uiState.value = currentState.copy(
            showFormFields = false,
            isLoading = true,
        )

        // Perform login
        viewModelScope.launch {
            megaApi?.let { api ->
                // Note: This is a placeholder. The actual login logic should be
                // implemented based on your MegaApi integration requirements.
                // You may need to use callbacks or suspend functions depending on
                // how your MegaApi is structured.
                try {
                    // Example: api.login(email, password, listener)
                    // For now, this is a placeholder structure
                } catch (e: Exception) {
                    // Handle error
                    handleLoginError(e.message ?: "Login failed")
                }
            } ?: run {
                handleLoginError("MegaApi not available")
            }
        }
    }

    private fun handleLoginError(errorMessage: String) {
        val context = getApplication<Application>()
        _uiState.value = _uiState.value.copy(
            showFormFields = true,
            isLoading = false,
        )
    }

    fun handleLoginSuccess() {
        // This will be called when login is successful
        // Navigation should be handled by the Activity/Composable
    }

    fun handleFetchNodesStart() {
        val context = getApplication<Application>()
        _uiState.value = _uiState.value.copy(
            showProgressBar = true
        )
    }

    fun handleFetchNodesProgress(progress: Int) {
        // Update progress if needed
        // The progress bar visibility is already handled
    }

    fun handleFetchNodesComplete(success: Boolean) {
        val context = getApplication<Application>()
        if (success) {
            handleLoginSuccess()
        } else {
            _uiState.value = _uiState.value.copy(
                showFormFields = true,
                isLoading = false,
                showProgressBar = false,
            )
        }
    }

    fun resetState() {
        val context = getApplication<Application>()
        _uiState.value = LoginUiState(
            email = "rsh+21@mega.co.nz",
            password = "",
            showProgressBar = false,
            showFormFields = true
        )
    }
}

