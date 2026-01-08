/**
 * NavigationScreen.kt
 * Compose screen for file browser navigation
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
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import nz.mega.android.bindingsample.R

/**
 * Main navigation screen composable that manages ViewModel and handles navigation logic.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NavigationScreen(
    onLogout: () -> Unit,
    savedParentHandle: Long? = null,
    modifier: Modifier = Modifier
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
    val currentUiStateForBack = uiState
    val canNavigateUp = when (currentUiStateForBack) {
        is NavigationUiState.Content -> currentUiStateForBack.parentHandle != -1L
        is NavigationUiState.Empty -> currentUiStateForBack.parentHandle != -1L
        else -> false
    }

    BackHandler(enabled = canNavigateUp) {
        viewModel.navigateUp()
    }

    NavigationActivityTheme {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        val currentState = uiState
                        Text(
                            text = when (currentState) {
                                is NavigationUiState.Content -> currentState.parentName
                                is NavigationUiState.Empty -> currentState.parentName
                                is NavigationUiState.Error -> currentState.parentName.ifEmpty { stringResource(R.string.cloud_drive) }
                                else -> stringResource(R.string.cloud_drive)
                            }
                        )
                    },
                    actions = {
                        TextButton(onClick = { viewModel.logout() }) {
                            Text(
                                text = stringResource(R.string.action_logout),
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                )
            }
        ) { paddingValues ->
            val currentState = uiState
            Box(
                modifier = modifier
                    .fillMaxSize()
                    .padding(paddingValues)
            ) {
                when (currentState) {
                    is NavigationUiState.Initial -> {
                        // Initial state - show loading
                        CircularProgressIndicator(
                            modifier = Modifier.align(Alignment.Center)
                        )
                    }

                    is NavigationUiState.Loading -> {
                        CircularProgressIndicator(
                            modifier = Modifier.align(Alignment.Center)
                        )
                    }

                    is NavigationUiState.Content -> {
                        if (currentState.nodes.isEmpty()) {
                            EmptyFolderView()
                        } else {
                            LazyColumn(
                                modifier = Modifier.fillMaxSize()
                            ) {
                                items(
                                    items = currentState.nodes,
                                    key = { it.node.handle }
                                ) { nodeInfo ->
                                    NodeListItem(
                                        nodeInfo = nodeInfo,
                                        onNodeClick = { viewModel.navigateToFolder(nodeInfo.node) }
                                    )
                                }
                            }
                        }
                    }

                    is NavigationUiState.Empty -> {
                        EmptyFolderView()
                    }

                    is NavigationUiState.Error -> {
                        Box(
                            modifier = Modifier.fillMaxSize(),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = currentState.errorMessage,
                                color = MaterialTheme.colorScheme.error,
                                style = MaterialTheme.typography.bodyLarge
                            )
                        }
                    }

                    is NavigationUiState.LoggingOut -> {
                        CircularProgressIndicator(
                            modifier = Modifier.align(Alignment.Center)
                        )
                    }

                    is NavigationUiState.LogoutSuccess -> {
                        // This state should trigger navigation, handled by LaunchedEffect above
                        CircularProgressIndicator(
                            modifier = Modifier.align(Alignment.Center)
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun NavigationActivityTheme(content: @Composable () -> Unit) {
    MaterialTheme {
        content()
    }
}

