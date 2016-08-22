/**
 * BrowserListAdapter.java
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
package nz.mega.android.bindingsample;

import java.text.DecimalFormat;
import java.util.ArrayList;

import nz.mega.sdk.MegaApiAndroid;
import nz.mega.sdk.MegaNode;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.ImageView;
import android.widget.RelativeLayout;
import android.widget.TextView;

public class BrowserListAdapter extends BaseAdapter {
	
	Context context;
	ArrayList<MegaNode> nodes;
	MegaApiAndroid megaApi;
	
	/* public static view holder class */
	public class ViewHolder {
		RelativeLayout layout;
		ImageView thumbnail;
		TextView fileName;
		TextView fileSize;
	}
		
	BrowserListAdapter (Context context, ArrayList<MegaNode> nodes, MegaApiAndroid megaApi){
		this.context = context;
		this.nodes = nodes;		
		this.megaApi = megaApi;
	}
	
	public void setNodes(ArrayList<MegaNode> nodes){
		this.nodes = nodes;
		notifyDataSetChanged();
	}
	
	@Override
	public int getCount() {
		return nodes.size();
	}

	@Override
	public Object getItem(int position) {
		return nodes.get(position);
	}

	@Override
	public long getItemId(int position) {
		return position;
	}

	@Override
	public View getView(int position, View convertView, ViewGroup parent) {
		
		ViewHolder holder;
		LayoutInflater inflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
		if (convertView == null) {
			convertView = inflater.inflate(R.layout.item_browser_list, parent,false);
			holder = new ViewHolder();
			
			holder.layout = (RelativeLayout) convertView.findViewById(R.id.item_layout);
			holder.thumbnail = (ImageView) convertView.findViewById(R.id.thumbnail);
			holder.fileName = (TextView) convertView.findViewById(R.id.filename);
			holder.fileSize = (TextView) convertView.findViewById(R.id.filesize);
			
			convertView.setTag(holder);
		}
		else{
			holder = (ViewHolder) convertView.getTag();
		}
		
		MegaNode node = (MegaNode) getItem(position);
		holder.fileName.setText(node.getName());
		
		if (node.isFile()){
			holder.fileSize.setText(getSizeString(node.getSize()));
			holder.thumbnail.setImageResource(R.drawable.mime_file);
		}
		else{
			holder.thumbnail.setImageResource(R.drawable.mime_folder);
			holder.fileSize.setText(getInfoFolder(node));
		}
		
		return convertView;
	}

	private String getSizeString(long size){
		String sizeString = "";
		DecimalFormat decf = new DecimalFormat("###.##");

		float KB = 1024;
		float MB = KB * 1024;
		float GB = MB * 1024;
		float TB = GB * 1024;
		
		if (size < KB){
			sizeString = size + " B";
		}
		else if (size < MB){
			sizeString = decf.format(size/KB) + " KB";
		}
		else if (size < GB){
			sizeString = decf.format(size/MB) + " MB";
		}
		else if (size < TB){
			sizeString = decf.format(size/GB) + " GB";
		}
		else{
			sizeString = decf.format(size/TB) + " TB";
		}
		
		return sizeString;
	}
	
	private String getInfoFolder(MegaNode n) {
		ArrayList<MegaNode> nL = megaApi.getChildren(n);

		int numFolders = 0;
		int numFiles = 0;

		for (int i = 0; i < nL.size(); i++) {
			MegaNode c = nL.get(i);
			if (c.isFolder()) {
				numFolders++;
			} else {
				numFiles++;
			}
		}

		String info = "";
		if (numFolders > 0) {
			info = numFolders
					+ " "
					+ context.getResources().getQuantityString(
							R.plurals.general_num_folders, numFolders);
			if (numFiles > 0) {
				info = info
						+ ", "
						+ numFiles
						+ " "
						+ context.getResources().getQuantityString(
								R.plurals.general_num_files, numFiles);
			}
		} else {
			info = numFiles
					+ " "
					+ context.getResources().getQuantityString(
							R.plurals.general_num_files, numFiles);
		}

		return info;
	}
}
