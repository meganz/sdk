/**
 * NodeListItem.kt
 * Compose component for displaying a file/folder node in the list
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

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import nz.mega.android.bindingsample.R
import java.text.DecimalFormat

/**
 * Format file size to human-readable string
 */
private fun formatFileSize(size: Long): String {
    val decf = DecimalFormat("###.##")

    val KB = 1024f
    val MB = KB * 1024
    val GB = MB * 1024
    val TB = GB * 1024

    return when {
        size < KB -> "$size B"
        size < MB -> "${decf.format(size / KB)} KB"
        size < GB -> "${decf.format(size / MB)} MB"
        size < TB -> "${decf.format(size / GB)} GB"
        else -> "${decf.format(size / TB)} TB"
    }
}

/**
 * Composable for displaying a file/folder node in the list
 */
@Composable
fun NodeListItem(
    nodeInfo: NodeDisplayInfo,
    onNodeClick: (NodeDisplayInfo) -> Unit,
    modifier: Modifier = Modifier
) {
    val node = nodeInfo.node
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(enabled = node.isFolder) {
                if (node.isFolder) {
                    onNodeClick(nodeInfo)
                }
            }
            .padding(horizontal = 5.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Thumbnail icon
        Icon(
            painter = painterResource(
                id = if (node.isFile) {
                    R.drawable.mime_file
                } else {
                    R.drawable.mime_folder
                }
            ),
            contentDescription = if (node.isFile) "File" else "Folder",
            modifier = Modifier.size(60.dp),
            tint = if (node.isFile) {
                MaterialTheme.colorScheme.primary
            } else {
                MaterialTheme.colorScheme.secondary
            }
        )

        Spacer(modifier = Modifier.width(3.dp))

        // File name and size/info
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(start = 3.dp)
        ) {
            Text(
                text = node.name,
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.padding(bottom = 4.dp)
            )

            // For files, show size. For folders, show folder info
            if (node.isFile) {
                Text(
                    text = formatFileSize(node.size),
                    style = MaterialTheme.typography.bodySmall,
                    color = Color(0xFF848484),
                    fontSize = 12.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            } else {
                // Show folder info if available
                Text(
                    text = nodeInfo.folderInfo ?: "Folder",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color(0xFF848484),
                    fontSize = 12.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

