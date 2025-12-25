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
import android.util.Log
import android.util.Patterns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import nz.mega.android.bindingsample.R
import nz.mega.android.bindingsample.data.LoginResult
import nz.mega.android.bindingsample.data.SdkRepository
import nz.mega.sdk.MegaApiAndroid

class MainActivityViewModel(application: Application) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow<LoginUiState>(LoginUiState.Initial())
    val uiState: StateFlow<LoginUiState> = _uiState.asStateFlow()

    private var megaApi: MegaApiAndroid? = null

    init {
        val app = application as? DemoAndroidApplication
        megaApi = app?.getMegaApi()
    }

    fun updateEmail(email: String) {
        val currentState = _uiState.value
        when (currentState) {
            is LoginUiState.Initial -> {
                _uiState.value = currentState.copy(
                    email = email,
                    emailError = null
                )
            }

            is LoginUiState.Error -> {
                _uiState.value = LoginUiState.Initial(
                    email = email,
                    password = currentState.password,
                    emailError = null,
                    passwordError = null
                )
            }

            else -> {
                // Ignore updates in other states
            }
        }
    }

    fun updatePassword(password: String) {
        val currentState = _uiState.value
        when (currentState) {
            is LoginUiState.Initial -> {
                _uiState.value = currentState.copy(
                    password = password,
                    passwordError = null
                )
            }

            is LoginUiState.Error -> {
                _uiState.value = LoginUiState.Initial(
                    email = currentState.email,
                    password = password,
                    emailError = null,
                    passwordError = null
                )
            }

            else -> {
                // Ignore updates in other states
            }
        }
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
        val email: String
        val password: String

        when (currentState) {
            is LoginUiState.Initial -> {
                email = currentState.email
                password = currentState.password
            }

            is LoginUiState.Error -> {
                email = currentState.email
                password = currentState.password
            }

            else -> return false
        }

        val emailError = validateEmail(email)
        val passwordError = validatePassword(password)

        _uiState.value = LoginUiState.Initial(
            email = email,
            password = password,
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
        val email: String
        val password: String

        when (currentState) {
            is LoginUiState.Initial -> {
                email = currentState.email.lowercase().trim()
                password = currentState.password
            }

            else -> {
                Log.e("MainActivityViewModel", "incorrect state after clicking login")
                return
            }
        }

        // Update UI state to LoggingIn
        _uiState.value = LoginUiState.LoggingIn(
            email = email,
            password = password
        )

        // Perform login
        viewModelScope.launch {
            SdkRepository.login(email, password).collect { result ->
                when {
                    result is LoginResult.LoginSuccess -> {
                        handleLoginSuccess()
                    }

                    result is LoginResult.LoginFailure -> {
                        handleLoginError(result.errorMessage, email, password)
                    }
                }
            }
        }
    }


    private fun handleLoginError(errorMessage: String, email: String, password: String) {
        _uiState.value = LoginUiState.Error(
            email = email,
            password = password,
            errorMessage = errorMessage
        )
    }

    fun handleLoginSuccess() {
        val currentState = _uiState.value
        val email = when (currentState) {
            is LoginUiState.LoggingIn -> currentState.email
            else -> ""
        }
        // Transition to FetchingNodes state
        _uiState.value = LoginUiState.FetchingNodes(email = email)
    }

    fun handleFetchNodesStart() {
        val currentState = _uiState.value
        val email = when (currentState) {
            is LoginUiState.LoggingIn -> currentState.email
            is LoginUiState.FetchingNodes -> currentState.email
            else -> ""
        }
        _uiState.value = LoginUiState.FetchingNodes(email = email)
    }

    fun handleFetchNodesProgress(progress: Int) {
        // Update progress if needed
        // The progress bar visibility is already handled by the FetchingNodes state
    }

    fun handleFetchNodesComplete(success: Boolean) {
        val currentState = _uiState.value
        val email = when (currentState) {
            is LoginUiState.FetchingNodes -> currentState.email
            else -> ""
        }

        if (success) {
            _uiState.value = LoginUiState.Success(email = email)
        } else {
            // If fetching nodes failed, go back to Initial state
            _uiState.value = LoginUiState.Initial(
                email = email,
                password = "",
                emailError = null,
                passwordError = null
            )
        }
    }

    fun resetState() {
        _uiState.value = LoginUiState.Initial(
            email = DEFAULT_EMAIL,
            password = "",
            emailError = null,
            passwordError = null
        )
    }
}


