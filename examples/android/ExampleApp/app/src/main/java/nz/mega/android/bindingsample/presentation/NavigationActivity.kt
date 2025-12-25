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
package nz.mega.android.bindingsample.presentation

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.AdapterView
import android.widget.AdapterView.OnItemClickListener
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import nz.mega.android.bindingsample.BrowserListAdapter
import nz.mega.android.bindingsample.R
import nz.mega.sdk.MegaApiAndroid
import nz.mega.sdk.MegaApiJava
import nz.mega.sdk.MegaError
import nz.mega.sdk.MegaNode
import nz.mega.sdk.MegaRequest
import nz.mega.sdk.MegaRequestListenerInterface

class NavigationActivity : Activity(), OnItemClickListener, MegaRequestListenerInterface {

    private lateinit var app: DemoAndroidApplication
    private lateinit var megaApi: MegaApiAndroid

    private lateinit var emptyText: TextView
    private lateinit var list: ListView

    private var nodes: ArrayList<MegaNode>? = null
    private var parentHandle: Long = -1

    private var adapter: BrowserListAdapter? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        app = application as DemoAndroidApplication
        megaApi = app.getMegaApi()

        setContentView(R.layout.activity_navigation)

        emptyText = findViewById(R.id.list_empty_text)
        list = findViewById(R.id.list_nodes)
        list.onItemClickListener = this

        if (savedInstanceState != null) {
            parentHandle = savedInstanceState.getLong("parentHandle")
        }

        var parentNode = megaApi?.getNodeByHandle(parentHandle)
        if (parentNode == null) {
            parentNode = megaApi?.getRootNode()
            title = getString(R.string.cloud_drive)
        } else {
            title = parentNode.name
        }

        nodes = parentNode?.let { megaApi?.getChildren(it) }
        if (nodes.isNullOrEmpty()) {
            emptyText.visibility = View.VISIBLE
            list.visibility = View.GONE
            emptyText.text = resources.getString(R.string.empty_folder)
        } else {
            emptyText.visibility = View.GONE
            list.visibility = View.VISIBLE
        }

        if (adapter == null) {
            adapter = BrowserListAdapter(this, nodes ?: ArrayList(), megaApi!!)
        }

        list.adapter = adapter
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putLong("parentHandle", parentHandle)
    }

    override fun onItemClick(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
        val n = nodes?.get(position) ?: return

        if (n.isFolder) {
            title = n.name
            parentHandle = n.handle
            nodes = megaApi?.getChildren(n)
            adapter?.setNodes(nodes ?: ArrayList())
            list.setSelection(0)

            if (adapter?.count == 0) {
                emptyText.visibility = View.VISIBLE
                list.visibility = View.GONE
                emptyText.text = resources.getString(R.string.empty_folder)
            } else {
                emptyText.visibility = View.GONE
                list.visibility = View.VISIBLE
            }
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        // Inflate the menu; this adds items to the action bar if it is present.
        menu.add(0, 0, 0, "Logout")
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        val id = item.itemId
        if (id == 0) {
            megaApi?.logout(this)
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    override fun onBackPressed() {
        val currentNode = megaApi?.getNodeByHandle(parentHandle)
        val parentNode = currentNode?.let { megaApi?.getParentNode(it) }

        if (parentNode != null) {
            parentHandle = parentNode.handle
            list.visibility = View.VISIBLE
            emptyText.visibility = View.GONE
            title = if (parentNode.type == MegaNode.TYPE_ROOT) {
                getString(R.string.cloud_drive)
            } else {
                parentNode.name
            }

            nodes = megaApi?.getChildren(parentNode)
            adapter?.setNodes(nodes ?: ArrayList())
            list.setSelection(0)
        } else {
            super.onBackPressed()
        }
    }

    override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {
        log("onRequestStart: ${request.requestString}")
    }

    override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {
        log("onRequestUpdate: ${request.requestString}")
    }

    override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        log("onRequestFinish: ${request.requestString}")

        if (request.type == MegaRequest.TYPE_LOGOUT) {
            if (e.errorCode == MegaError.API_OK) {
                Toast.makeText(this, resources.getString(R.string.logout_success), Toast.LENGTH_LONG).show()
                val intent = Intent(this, MainActivity::class.java)
                intent.flags = Intent.FLAG_ACTIVITY_CLEAR_TOP
                startActivity(intent)
                finish()
            } else {
                Toast.makeText(this, e.errorString, Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun onRequestTemporaryError(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        log("onRequestTemporaryError: ${request.requestString}")
    }

    companion object {
        fun log(message: String) {
            MegaApiAndroid.log(MegaApiAndroid.LOG_LEVEL_INFO, message, "NavigationActivity")
        }
    }
}

