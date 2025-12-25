/**
 * BrowserListAdapter.kt
 * Listview adapter
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
package nz.mega.android.bindingsample

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.ImageView
import android.widget.RelativeLayout
import android.widget.TextView
import nz.mega.sdk.MegaApiAndroid
import nz.mega.sdk.MegaNode
import java.text.DecimalFormat
import java.util.ArrayList

class BrowserListAdapter(
    private val context: Context,
    private var nodes: ArrayList<MegaNode>,
    private val megaApi: MegaApiAndroid
) : BaseAdapter() {

    /* public static view holder class */
    class ViewHolder {
        lateinit var layout: RelativeLayout
        lateinit var thumbnail: ImageView
        lateinit var fileName: TextView
        lateinit var fileSize: TextView
    }

    fun setNodes(nodes: ArrayList<MegaNode>) {
        this.nodes = nodes
        notifyDataSetChanged()
    }

    override fun getCount(): Int {
        return nodes.size
    }

    override fun getItem(position: Int): Any {
        return nodes[position]
    }

    override fun getItemId(position: Int): Long {
        return position.toLong()
    }

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val holder: ViewHolder
        val inflater = context.getSystemService(Context.LAYOUT_INFLATER_SERVICE) as LayoutInflater
        val view = convertView ?: run {
            val newView = inflater.inflate(R.layout.item_browser_list, parent, false)
            val newHolder = ViewHolder()
            newHolder.layout = newView.findViewById(R.id.item_layout)
            newHolder.thumbnail = newView.findViewById(R.id.thumbnail)
            newHolder.fileName = newView.findViewById(R.id.filename)
            newHolder.fileSize = newView.findViewById(R.id.filesize)
            newView.tag = newHolder
            newView
        }

        holder = view.tag as ViewHolder

        val node = getItem(position) as MegaNode
        holder.fileName.text = node.name

        if (node.isFile) {
            holder.fileSize.text = getSizeString(node.size)
            holder.thumbnail.setImageResource(R.drawable.mime_file)
        } else {
            holder.thumbnail.setImageResource(R.drawable.mime_folder)
            holder.fileSize.text = getInfoFolder(node)
        }

        return view
    }

    private fun getSizeString(size: Long): String {
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

    private fun getInfoFolder(n: MegaNode): String {
        val nL = megaApi.getChildren(n)

        var numFolders = 0
        var numFiles = 0

        for (c in nL) {
            if (c.isFolder) {
                numFolders++
            } else {
                numFiles++
            }
        }

        val info = when {
            numFolders > 0 -> {
                val folderInfo = "$numFolders ${context.resources.getQuantityString(R.plurals.general_num_folders, numFolders)}"
                if (numFiles > 0) {
                    "$folderInfo, $numFiles ${context.resources.getQuantityString(R.plurals.general_num_files, numFiles)}"
                } else {
                    folderInfo
                }
            }
            else -> "$numFiles ${context.resources.getQuantityString(R.plurals.general_num_files, numFiles)}"
        }

        return info
    }
}

