/*
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,\
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * @copyright Simplified (2-clause) BSD License.
 * You should have received a copy of the license along with this
 * program.
 */
 package nz.mega.sdk;

import java.util.ArrayList;

public class MegaChildren {

 private ArrayList<MegaNode> fileList = new ArrayList<MegaNode>();
 private ArrayList<MegaNode> folderList = new ArrayList<MegaNode>();

 public ArrayList<MegaNode> getFileList(){
  return fileList;
 }

 public ArrayList<MegaNode> getFolderList(){
  return folderList;
 }

 public void setFileList (ArrayList<MegaNode> fileList){
  this.fileList = fileList;
 }

 public void setFolderList (ArrayList<MegaNode> folderList){
  this.folderList = folderList;
 }
}
