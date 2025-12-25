/**
 * MainActivity2.kt
 * Initial activity of the demo app
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

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier

class MainActivity2 : ComponentActivity() {

    private val viewModel: MainActivityViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val uiState by viewModel.uiState.collectAsStateWithLifecycle()

            MainActivity2Theme {
                Scaffold(
                    modifier = Modifier.fillMaxSize()
                ) { paddingValues ->
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(paddingValues),
                        contentAlignment = Alignment.Center
                    ) {
                        LoginScreen(
                            email = uiState.email,
                            password = uiState.password,
                            emailError = uiState.emailError,
                            passwordError = uiState.passwordError,
                            showProgressBar = uiState.showProgressBar,
                            showFormFields = uiState.showFormFields,
                            onEmailChange = { viewModel.updateEmail(it) },
                            onPasswordChange = { viewModel.updatePassword(it) },
                            onLoginClick = { email, password ->
                                viewModel.onLoginClick()
                            }
                        )
                    }
                }
            }
        }
    }
}


@Composable
fun MainActivity2Theme(content: @Composable () -> Unit) {
    MaterialTheme {
        content()
    }
}

