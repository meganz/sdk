#import "Helper.h"
#import "MEGASdkManager.h"

@implementation Helper

+ (UIImage *)imageForNode:(MEGANode *)node {
    
    NSDictionary *dictionary = @{@"3ds":@"3D.png",
                                 @"3dm":@"3D.png",
                                 @"3fr":@"raw.png",
                                 @"3g2":@"video.png",
                                 @"3gp":@"video.png",
                                 @"7z":@"compressed.png",
                                 @"aac":@"audio.png",
                                 @"ac3":@"audio.png",
                                 @"accdb":@"database.png",
                                 @"aep":@"aftereffects.png",
                                 @"aet":@"aftereffects.png",
                                 @"ai":@"illustrator.png",
                                 @"aif":@"audio.png",
                                 @"aiff":@"audio.png",
                                 @"ait":@"illustrator.png",
                                 @"ans":@"text.png",
                                 @"apk":@"executable.png",
                                 @"app":@"executable.png",
                                 @"arw":@"raw.png",
                                 @"as":@"fla_lang.png",
                                 @"asc":@"fla_lang.png",
                                 @"ascii":@"text.png",
                                 @"asf":@"video.png",
                                 @"asp":@"web_lang.png",
                                 @"aspx":@"web_lang.png",
                                 @"asx":@"playlist.png",
                                 @"avi":@"video.png",
                                 @"bay":@"raw.png",
                                 @"bmp":@"graphic.png",
                                 @"bz2":@"compressed.png",
                                 @"c":@"sourcecode.png",
                                 @"cc":@"sourcecode.png",
                                 @"cdr":@"vector.png",
                                 @"cgi":@"web_lang.png",
                                 @"class":@"java.png",
                                 @"com":@"executable.png",
                                 @"cpp":@"sourcecode.png",
                                 @"cr2":@"raw.png",
                                 @"css":@"web_data.png",
                                 @"cxx":@"sourcecode.png",
                                 @"dcr":@"raw.png",
                                 @"db":@"database.png",
                                 @"dbf":@"database.png",
                                 @"dhtml":@"html.png",
                                 @"dll":@"sourcecode.png",
                                 @"dng":@"raw.png",
                                 @"doc":@"word.png",
                                 @"docx":@"word.png",
                                 @"dotx":@"word.png",
                                 @"dwg":@"cad.png",
                                 @"dwt":@"dreamweaver.png",
                                 @"dxf":@"cad.png",
                                 @"eps":@"vector.png",
                                 @"exe":@"executable.png",
                                 @"fff":@"raw.png",
                                 @"fla":@"flash.png",
                                 @"flac":@"audio.png",
                                 @"flv":@"flash_video.png",
                                 @"fnt":@"font.png",
                                 @"fon":@"font.png",
                                 @"gadget":@"executable.png",
                                 @"gif":@"graphic.png",
                                 @"gpx":@"gis.png",
                                 @"gsheet":@"spreadsheet.png",
                                 @"gz":@"compressed.png",
                                 @"h":@"sourcecode.png",
                                 @"hpp":@"sourcecode.png",
                                 @"htm":@"html.png",
                                 @"html":@"html.png",
                                 @"iff":@"audio.png",
                                 @"inc":@"web_lang.png",
                                 @"indd":@"indesign.png",
                                 @"jar":@"java.png",
                                 @"java":@"java.png",
                                 @"jpeg":@"image.png",
                                 @"jpg":@"image.png",
                                 @"js":@"web_data.png",
                                 @"key":@"generic.png",
                                 @"kml":@"gis.png",
                                 @"log":@"text.png",
                                 @"m":@"sourcecode.png",
                                 @"mm":@"sourcecode.png",
                                 @"m3u":@"playlist.png",
                                 @"m4a":@"audio.png",
                                 @"max":@"3D.png",
                                 @"mdb":@"database.png",
                                 @"mef":@"raw.png",
                                 @"mid":@"midi.png",
                                 @"midi":@"midi.png",
                                 @"mkv":@"video.png",
                                 @"mov":@"video.png",
                                 @"mp3":@"audio.png",
                                 @"mp4":@"video.png",
                                 @"mpeg":@"video.png",
                                 @"mpg":@"video.png",
                                 @"mrw":@"raw.png",
                                 @"msi":@"executable.png",
                                 @"nb":@"spreadsheet.png",
                                 @"numbers":@"spreadsheet.png",
                                 @"nef":@"raw.png",
                                 @"obj":@"3D.png",
                                 @"odp":@"generic.png",
                                 @"ods":@"spreadsheet.png",
                                 @"odt":@"text.png",
                                 @"ogv":@"video.png",
                                 @"otf":@"font.png",
                                 @"ots":@"spreadsheet.png",
                                 @"orf":@"raw.png",
                                 @"pages":@"text.png",
                                 @"pcast":@"podcast.png",
                                 @"pdb":@"database.png",
                                 @"pdf":@"pdf.png",
                                 @"pef":@"raw.png",
                                 @"php":@"web_lang.png",
                                 @"php3":@"web_lang.png",
                                 @"php4":@"web_lang.png",
                                 @"php5":@"web_lang.png",
                                 @"phtml":@"web_lang.png",
                                 @"pl":@"web_lang.png",
                                 @"pls":@"playlist.png",
                                 @"png":@"graphic.png",
                                 @"ppj":@"premiere.png",
                                 @"pps":@"powerpoint.png",
                                 @"ppt":@"powerpoint.png",
                                 @"pptx":@"powerpoint.png",
                                 @"prproj":@"premiere.png",
                                 @"psb":@"photoshop.png",
                                 @"psd":@"photoshop.png",
                                 @"py":@"web_lang.png",
                                 @"ra":@"real_audio.png",
                                 @"ram":@"real_audio.png",
                                 @"rar":@"compressed.png",
                                 @"rm":@"real_audio.png",
                                 @"rtf":@"text.png",
                                 @"rw2":@"raw.png",
                                 @"rwl":@"raw.png",
                                 @"sh":@"sourcecode.png",
                                 @"shtml":@"web_data.png",
                                 @"sitx":@"compressed.png",
                                 @"sql":@"database.png",
                                 @"srf":@"raw.png",
                                 @"srt":@"video_subtitles.png",
                                 @"stl":@"3D.png",
                                 @"svg":@"vector.png",
                                 @"svgz":@"vector.png",
                                 @"swf":@"swf.png",
                                 @"tar":@"compressed.png",
                                 @"tbz":@"compressed.png",
                                 @"tga":@"graphic.png",
                                 @"tgz":@"compressed.png",
                                 @"tif":@"graphic.png",
                                 @"tiff":@"graphic.png",
                                 @"torrent":@"torrent.png",
                                 @"ttf":@"font.png",
                                 @"txt":@"text.png",
                                 @"vcf":@"vcard.png",
                                 @"vob":@"video_vob.png",
                                 @"wav":@"audio.png",
                                 @"webm":@"video.png",
                                 @"wma":@"audio.png",
                                 @"wmv":@"video.png",
                                 @"wpd":@"text.png",
                                 @"wps":@"word.png",
                                 @"xhtml":@"html.png",
                                 @"xlr":@"spreadsheet.png",
                                 @"xls":@"excel.png",
                                 @"xlsx":@"excel.png",
                                 @"xlt":@"excel.png",
                                 @"xltm":@"excel.png",
                                 @"xml":@"web_data.png",
                                 @"zip":@"compressed.png"};
    
    MEGANodeType nodeType = [node type];
    
    switch (nodeType) {
        case MEGANodeTypeFolder: {
            if ([[MEGASdkManager sharedMEGASdk] isSharedNode:node])
                return [UIImage imageNamed:@"folder_shared"];
            else
                return [UIImage imageNamed:@"folder"];
            }
                
        case MEGANodeTypeRubbish:
            return [UIImage imageNamed:@"folder"];
            
        case MEGANodeTypeFile: {
            NSString *im = [dictionary valueForKey:[node name].lowercaseString.pathExtension];
            if (im && im.length>0) {
                return [UIImage imageNamed:im];
            }
        }
            
        default:
            return [UIImage imageNamed:@"generic"];
    }
    
}

+ (NSString *)pathForNode:(MEGANode *)node searchPath:(NSSearchPathDirectory)path directory:(NSString *)directory {
    
    NSString *extension = [@"." stringByAppendingString:[[node name] pathExtension]];
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(path, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [[node base64Handle] stringByAppendingString:extension];
    NSString *destinationFilePath = nil;
    destinationFilePath = [directory isEqualToString:@""] ? [destinationPath stringByAppendingPathComponent:fileName]
    :[[destinationPath stringByAppendingPathComponent:directory] stringByAppendingPathComponent:fileName];
    
    return destinationFilePath;
}

+ (NSString *)pathForNode:(MEGANode *)node searchPath:(NSSearchPathDirectory)path {
    return [self pathForNode:node searchPath:path directory:@""];
}

+ (NSString *)pathForUser:(MEGAUser *)user searchPath:(NSSearchPathDirectory)path directory:(NSString *)directory {
    
    NSString *destinationPath = [NSSearchPathForDirectoriesInDomains(path, NSUserDomainMask, YES) objectAtIndex:0];
    NSString *fileName = [[user email] stringByAppendingString:@".png"];
    NSString *destinationFilePath = nil;
    destinationFilePath = [directory isEqualToString:@""] ? [destinationPath stringByAppendingPathComponent:fileName]
    :[[destinationPath stringByAppendingPathComponent:directory] stringByAppendingPathComponent:fileName];
    
    return destinationFilePath;
}

@end
