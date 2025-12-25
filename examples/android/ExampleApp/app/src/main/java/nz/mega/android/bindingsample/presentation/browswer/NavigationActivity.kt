/**
 * NavigationActivity.kt
 * Activity that shows the MEGA file and allow navigation through folders
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

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.activity.compose.BackHandler
import androidx.compose.material3.MaterialTheme
import nz.mega.android.bindingsample.presentation.login.MainActivity
import nz.mega.sdk.MegaApiAndroid

class NavigationActivity : ComponentActivity() {

    private val viewModel: NavigationViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Restore saved state if available and load initial nodes
        val savedParentHandle = savedInstanceState?.getLong("parentHandle", -1)?.takeIf { it != -1L }
        viewModel.handleSavedState(savedParentHandle)

        setContent {
            val uiState by viewModel.uiState.collectAsStateWithLifecycle()

            // Handle logout success - navigate to MainActivity
            LaunchedEffect(uiState) {
                if (uiState is NavigationUiState.LogoutSuccess) {
                    val intent = Intent(this@NavigationActivity, MainActivity::class.java)
                    intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP
                    startActivity(intent)
                    finish()
                }
            }

            // Handle back button navigation
            // We can navigate up if we're not at root
            // The ViewModel will handle checking if we're at root
            val currentUiState = uiState
            val canNavigateUp = when (currentUiState) {
                is NavigationUiState.Content -> currentUiState.parentHandle != -1L
                is NavigationUiState.Empty -> currentUiState.parentHandle != -1L
                else -> false
            }

            BackHandler(enabled = canNavigateUp) {
                viewModel.navigateUp()
            }

            NavigationActivityTheme {
                NavigationScreen(
                    uiState = uiState,
                    onNodeClick = { nodeInfo ->
                        viewModel.navigateToFolder(nodeInfo.node)
                    },
                    onLogoutClick = {
                        viewModel.logout()
                    }
                )
            }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        // Save current parent handle for state restoration
        val currentState = viewModel.uiState.value
        val parentHandle = when (currentState) {
            is NavigationUiState.Content -> currentState.parentHandle
            is NavigationUiState.Empty -> currentState.parentHandle
            else -> -1L
        }
        if (parentHandle != -1L) {
            outState.putLong("parentHandle", parentHandle)
        }
    }

    companion object {
        fun log(message: String) {
            MegaApiAndroid.log(MegaApiAndroid.LOG_LEVEL_INFO, message, "NavigationActivity")
        }
    }
}

@Composable
fun NavigationActivityTheme(content: @Composable () -> Unit) {
    MaterialTheme {
        content()
    }
}

