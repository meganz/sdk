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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import nz.mega.android.bindingsample.R

/**
 * Main navigation screen composable
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NavigationScreen(
    uiState: NavigationUiState,
    onNodeClick: (NodeDisplayInfo) -> Unit,
    onLogoutClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = when (uiState) {
                            is NavigationUiState.Content -> uiState.parentName
                            is NavigationUiState.Empty -> uiState.parentName
                            is NavigationUiState.Error -> uiState.parentName.ifEmpty { stringResource(R.string.cloud_drive) }
                            else -> stringResource(R.string.cloud_drive)
                        }
                    )
                },
                actions = {
                    TextButton(onClick = onLogoutClick) {
                        Text(
                            text = stringResource(R.string.action_logout),
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                }
            )
        }
    ) { paddingValues ->
        Box(
            modifier = modifier
                .fillMaxSize()
                .padding(paddingValues)
        ) {
            when (uiState) {
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
                    if (uiState.nodes.isEmpty()) {
                        EmptyFolderView()
                    } else {
                        LazyColumn(
                            modifier = Modifier.fillMaxSize()
                        ) {
                            items(
                                items = uiState.nodes,
                                key = { it.node.handle }
                            ) { nodeInfo ->
                                NodeListItem(
                                    nodeInfo = nodeInfo,
                                    onNodeClick = onNodeClick
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
                            text = uiState.errorMessage,
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
                    // This state should trigger navigation, handled by LaunchedEffect in Activity
                    CircularProgressIndicator(
                        modifier = Modifier.align(Alignment.Center)
                    )
                }
            }
        }
    }
}

