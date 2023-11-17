/**
* @file Helper.swift
* @brief Helper
*
* (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

import UIKit
import MEGASdk

class Helper {
    
    struct workaround { static let icons : [String : String] = ["3ds":"3D",
        "3dm":"3D",
        "3fr":"raw",
        "3g2":"video",
        "3gp":"video",
        "7z":"compressed",
        "aac":"audio",
        "ac3":"audio",
        "accdb":"database",
        "aep":"aftereffects",
        "aet":"aftereffects",
        "ai":"illustrator",
        "aif":"audio",
        "aiff":"audio",
        "ait":"illustrator",
        "ans":"text",
        "apk":"executable",
        "app":"executable",
        "arw":"raw",
        "as":"fla_lang",
        "asc":"fla_lang",
        "ascii":"text",
        "asf":"video",
        "asp":"web_lang",
        "aspx":"web_lang",
        "asx":"playlist",
        "avi":"video",
        "bay":"raw",
        "bmp":"graphic",
        "bz2":"compressed",
        "c":"sourcecode",
        "cc":"sourcecode",
        "cdr":"vector",
        "cgi":"web_lang",
        "class":"java",
        "com":"executable",
        "cpp":"sourcecode",
        "cr2":"raw",
        "css":"web_data",
        "cxx":"sourcecode",
        "dcr":"raw",
        "db":"database",
        "dbf":"database",
        "dhtml":"html",
        "dll":"sourcecode",
        "dng":"raw",
        "doc":"word",
        "docx":"word",
        "dotx":"word",
        "dwg":"cad",
        "dwt":"dreamweaver",
        "dxf":"cad",
        "eps":"vector",
        "exe":"executable",
        "fff":"raw",
        "fla":"flash",
        "flac":"audio",
        "flv":"flash_video",
        "fnt":"font",
        "fon":"font",
        "gadget":"executable",
        "gif":"graphic",
        "gpx":"gis",
        "gsheet":"spreadsheet",
        "gz":"compressed",
        "h":"sourcecode",
        "hpp":"sourcecode",
        "htm":"html",
        "html":"html",
        "iff":"audio",
        "inc":"web_lang",
        "indd":"indesign",
        "jar":"java",
        "java":"java",
        "jpeg":"image",
        "jpg":"image",
        "js":"web_data",
        "key":"generic",
        "kml":"gis",
        "log":"text",
        "m":"sourcecode",
        "mm":"sourcecode",
        "m3u":"playlist",
        "m4a":"audio",
        "max":"3D",
        "mdb":"database",
        "mef":"raw",
        "mid":"midi",
        "midi":"midi",
        "mkv":"video",
        "mov":"video",
        "mp3":"audio",
        "mp4":"video",
        "mpeg":"video",
        "mpg":"video",
        "mrw":"raw",
        "msi":"executable",
        "nb":"spreadsheet",
        "numbers":"spreadsheet",
        "nef":"raw",
        "obj":"3D",
        "odp":"generic",
        "ods":"spreadsheet",
        "odt":"text",
        "ogv":"video",
        "otf":"font",
        "ots":"spreadsheet",
        "orf":"raw",
        "pages":"text",
        "pcast":"podcast",
        "pdb":"database",
        "pdf":"pdf",
        "pef":"raw",
        "php":"web_lang",
        "php3":"web_lang",
        "php4":"web_lang",
        "php5":"web_lang",
        "phtml":"web_lang",
        "pl":"web_lang",
        "pls":"playlist",
        "png":"graphic",
        "ppj":"premiere",
        "pps":"powerpoint",
        "ppt":"powerpoint",
        "pptx":"powerpoint",
        "prproj":"premiere",
        "psb":"photoshop",
        "psd":"photoshop",
        "py":"web_lang",
        "ra":"real_audio",
        "ram":"real_audio",
        "rar":"compressed",
        "rm":"real_audio",
        "rtf":"text",
        "rw2":"raw",
        "rwl":"raw",
        "sh":"sourcecode",
        "shtml":"web_data",
        "sitx":"compressed",
        "sql":"database",
        "srf":"raw",
        "srt":"video_subtitles",
        "stl":"3D",
        "svg":"vector",
        "svgz":"vector",
        "swf":"swf",
        "tar":"compressed",
        "tbz":"compressed",
        "tga":"graphic",
        "tgz":"compressed",
        "tif":"graphic",
        "tiff":"graphic",
        "torrent":"torrent",
        "ttf":"font",
        "txt":"text",
        "vcf":"vcard",
        "vob":"video_vob",
        "wav":"audio",
        "webm":"video",
        "wma":"audio",
        "wmv":"video",
        "wpd":"text",
        "wps":"word",
        "xhtml":"html",
        "xlr":"spreadsheet",
        "xls":"excel",
        "xlsx":"excel",
        "xlt":"excel",
        "xltm":"excel",
        "xml":"web_data",
        "zip":"compressed"]
    }

    
    class func imageForNode(_ node : MEGANode) -> UIImage {        
        switch node.type {
        case MEGANodeType.folder:
            if node.isInShare() {
                return UIImage(named: "folder_shared")!
            } else {
                return UIImage(named: "folder")!
            }
            
        case MEGANodeType.file:
            let im = workaround.icons[(node.name!.lowercased() as NSString).pathExtension]
            if im != nil {
                return UIImage(named: im!)!
            }
            
            
        default:
            return UIImage(named: "generic")!
        }
        
        return UIImage(named: "generic")!
    }
    
    class func pathForNode(_ node : MEGANode, path : FileManager.SearchPathDirectory, directory : String) -> String {
        let destinationPath : String = NSSearchPathForDirectoriesInDomains(path, FileManager.SearchPathDomainMask.userDomainMask, true)[0] 
        let filename = node.base64Handle
        let destinationFilePath = directory == "" ? (destinationPath as NSString).appendingPathComponent(filename!) : ((destinationPath as NSString).appendingPathComponent(directory) as NSString).appendingPathComponent(filename!)
        
        return destinationFilePath
    }
    
    class func pathForNode(_ node : MEGANode, path : FileManager.SearchPathDirectory) -> String {
        return pathForNode(node, path: path, directory: "")
    }
    
    class func pathForUser(_ user : MEGAUser, path : FileManager.SearchPathDirectory, directory : String) -> String {
        let destinationPath : String = NSSearchPathForDirectoriesInDomains(path, FileManager.SearchPathDomainMask.userDomainMask, true)[0] 
        let filename = user.email
        let destinationFilePath = directory == "" ? (destinationPath as NSString).appendingPathComponent(filename!) : ((destinationPath as NSString).appendingPathComponent(directory) as NSString).appendingPathComponent(filename!)
        
        return destinationFilePath
    }
    
}
