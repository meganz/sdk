/**
 * NavigationScreenWrapper.kt
 * Composable wrapper for NavigationScreen that manages ViewModel and navigation logic
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
import androidx.activity.compose.BackHandler
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel

/**
 * Composable wrapper that manages NavigationViewModel and handles navigation callbacks.
 * This replaces the NavigationActivity logic.
 */
@Composable
fun NavigationScreenWrapper(
    onLogout: () -> Unit,
    savedParentHandle: Long? = null
) {
    val application = androidx.compose.ui.platform.LocalContext.current.applicationContext as Application
    val viewModel: NavigationViewModel = viewModel(
        factory = object : androidx.lifecycle.ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
                return NavigationViewModel(application) as T
            }
        }
    )

    val uiState by viewModel.uiState.collectAsStateWithLifecycle()

    // Handle initial load and saved state restoration
    LaunchedEffect(Unit) {
        if (uiState is NavigationUiState.Initial) {
            if (savedParentHandle != null) {
                viewModel.handleSavedState(savedParentHandle)
            } else {
                viewModel.loadNodes(-1)
            }
        }
    }

    // Handle logout success - trigger navigation callback
    LaunchedEffect(uiState) {
        if (uiState is NavigationUiState.LogoutSuccess) {
            onLogout()
        }
    }

    // Handle back button navigation
    // We can navigate up if we're not at root
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

@Composable
fun NavigationActivityTheme(content: @Composable () -> Unit) {
    MaterialTheme {
        content()
    }
}

