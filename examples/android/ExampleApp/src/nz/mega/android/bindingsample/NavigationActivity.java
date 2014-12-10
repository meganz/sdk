/**
 * NavigationActivity.java
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
package nz.mega.android.bindingsample;

import java.util.ArrayList;

import nz.mega.sdk.MegaApiAndroid;
import nz.mega.sdk.MegaApiJava;
import nz.mega.sdk.MegaError;
import nz.mega.sdk.MegaNode;
import nz.mega.sdk.MegaRequest;
import nz.mega.sdk.MegaRequestListenerInterface;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

public class NavigationActivity extends Activity implements OnItemClickListener, MegaRequestListenerInterface{

	DemoAndroidApplication app;
	MegaApiAndroid megaApi;
		
	TextView emptyText;
	ListView list;
	
	ArrayList<MegaNode> nodes;
	long parentHandle = -1;
	
	BrowserListAdapter adapter;
	
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		
		app = (DemoAndroidApplication) getApplication();
		if (megaApi == null){
			megaApi = app.getMegaApi();
		}
		
		setContentView(R.layout.activity_navigation);
		
		emptyText = (TextView) findViewById(R.id.list_empty_text);
		list = (ListView) findViewById(R.id.list_nodes);
		list.setOnItemClickListener(this);
		
		if (savedInstanceState != null){
			parentHandle = savedInstanceState.getLong("parentHandle");
		}

		MegaNode parentNode = megaApi.getNodeByHandle(parentHandle);
		if (parentNode == null){
			parentNode = megaApi.getRootNode();
			setTitle(getString(R.string.cloud_drive));
		}
		else{
			setTitle(parentNode.getName());
		}
		
		nodes = megaApi.getChildren(parentNode);
		if (nodes.size() == 0){
			emptyText.setVisibility(View.VISIBLE);
			list.setVisibility(View.GONE);
			emptyText.setText(getResources().getString(R.string.empty_folder));
		}
		else{
			emptyText.setVisibility(View.GONE);
			list.setVisibility(View.VISIBLE);
		}
		
		if (adapter == null){
			adapter = new BrowserListAdapter(this, nodes, megaApi);
		}
		
		list.setAdapter(adapter);
	}
	
	@Override
	protected void onSaveInstanceState(Bundle outState) {
		super.onSaveInstanceState(outState);
	    outState.putLong("parentHandle", parentHandle);
	}
	 
	@Override
	public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
		
		MegaNode n = nodes.get(position);
		
		if (n.isFolder()){		
			setTitle(n.getName());
			parentHandle = n.getHandle();
			nodes = megaApi.getChildren(n);
			adapter.setNodes(nodes);
			list.setSelection(0);
			
			if (adapter.getCount() == 0){
				emptyText.setVisibility(View.VISIBLE);
				list.setVisibility(View.GONE);
				emptyText.setText(getResources().getString(R.string.empty_folder));
			}
			else{
				emptyText.setVisibility(View.GONE);
				list.setVisibility(View.VISIBLE);
			}
		}
	}
	
	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		// Inflate the menu; this adds items to the action bar if it is present.
		menu.add(0, 0, 0, "Logout");
		return true;
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item) {
		// Handle action bar item clicks here. The action bar will
		// automatically handle clicks on the Home/Up button, so long
		// as you specify a parent activity in AndroidManifest.xml.
		int id = item.getItemId();
		if (id == 0) {
			megaApi.logout(this);
			return true;
		}
		return super.onOptionsItemSelected(item);
	}
	
	@Override
	public void onBackPressed() {
		MegaNode parentNode = megaApi.getParentNode(megaApi.getNodeByHandle(parentHandle));
		
		if (parentNode != null){
			parentHandle = parentNode.getHandle();
			list.setVisibility(View.VISIBLE);
			emptyText.setVisibility(View.GONE);		
			if (parentNode.getType() == MegaNode.TYPE_ROOT){
				setTitle(getString(R.string.cloud_drive));
			}
			else{
				setTitle(parentNode.getName());	
			}
			
			nodes = megaApi.getChildren(parentNode);
			adapter.setNodes(nodes);
			list.setSelection(0);
		}
		else{
			super.onBackPressed();
		}
	}

	@Override
	public void onRequestStart(MegaApiJava api, MegaRequest request) {
		log("onRequestStart: " + request.getRequestString());
	}

	@Override
	public void onRequestUpdate(MegaApiJava api, MegaRequest request) {
		log("onRequestUpdate: " + request.getRequestString());
	}

	@Override
	public void onRequestFinish(MegaApiJava api, MegaRequest request,
			MegaError e) {
		log("onRequestFinish: " + request.getRequestString());
		
		if (request.getType() == MegaRequest.TYPE_LOGOUT){
			if (e.getErrorCode() == MegaError.API_OK){
				Toast.makeText(this, getResources().getString(R.string.logout_success), Toast.LENGTH_LONG).show();
				Intent intent = new Intent(this, MainActivity.class);
				intent.setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
				startActivity(intent);
				finish();
			}
			else{
				Toast.makeText(this, e.getErrorString(), Toast.LENGTH_LONG).show();
			}
		}
	}

	@Override
	public void onRequestTemporaryError(MegaApiJava api, MegaRequest request,
			MegaError e) {
		log("onRequestTemporaryError: " + request.getRequestString());
	}
	
	public static void log(String message) {
		MegaApiAndroid.log(MegaApiAndroid.LOG_LEVEL_INFO, message, "NavigationActivity");
	}

}
