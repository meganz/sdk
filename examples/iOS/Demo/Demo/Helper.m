#import "Helper.h"

#define imagesSet       [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define videoSet        [[NSSet alloc] initWithObjects:/*@"mkv",*/ @"avi", @"mp4", @"m4v", @"mpg", @"mpeg", @"mov", @"3gp",/*@"aaf",*/ nil]
#define docSet          [[NSSet alloc] initWithObjects:@"txt", @"doc", @"pdf", @"odt", @"xml", @"json",@"csv",@"docx",@"pages",@"odt",@"ott",@"xps", nil]
#define archiveSet      [[NSSet alloc] initWithObjects:@"zip", @"rar", @"tar", @"gz", @"tar.gz",@"bzip",@"bzip2",@"gzip",@"lzip",@"ar",@"cpio",@"shar",@"xz",@"7z",@"ace",@"arc",@"arj",@"cab",@"dmg",@"lha",@"sit", nil]
#define audioSet        [[NSSet alloc] initWithObjects:@"wav",@"mp3",@"mp4",@"aiff",@"au",@"flac",@"m4a",@"aac", nil]

#define isImage(n)        [imagesSet containsObject:n]
#define isVideo(n)        [videoSet containsObject:n]
#define isAudio(n)        [audioSet containsObject:n]
#define isDocument(n)     [docSet containsObject:n]
#define isArchive(n)      [archiveSet containsObject:n]

@interface Helper () {
    
}

@end

@implementation Helper

+ (UIImage *)imageForNode:(MEGANode *)node {
    MEGANodeType nodeType = [node type];
    
    if (!node) {
        return [UIImage imageNamed:@"generic"];
    } else if (nodeType == MEGANodeTypeFolder) {
        return [UIImage imageNamed:@"folder"];
    } else if (nodeType == MEGANodeTypeRubbish) {
        return [UIImage imageNamed:@"folder"];
    } else if (nodeType == MEGANodeTypeFile) {
        NSString *im = [[self iconsTypesDictionary] valueForKey:[node name].lowercaseString.pathExtension];
        if (im && im.length>0) {
            return [UIImage imageNamed:im];
        }
    }
    
    return [UIImage imageNamed:@"generic"];
}


+ (NSDictionary *)iconsTypesDictionary {
    NSDictionary *dictionary = @{@"3ds":@"3D.png",
                            @"3dm":@"3D.png",
                            @"3fr":@"raw.png",
                            @"3g2":@"video.png",
                            @"3gp":@"video.png",
                            @"7z":@"compressed.png",
                            @"accdb":@"database.png",
                            @"aep":@"aftereffects.png",
                            @"aet":@"aftereffects.png",
                            @"ai":@"Illustrator.png",
                            @"aif":@"audio.png",
                            @"aiff":@"audio.png",
                            @"ait":@"Illustrator.png",
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
                            @"gpx":@"GIS.png",
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
                            @"key":@"powerpoint.png",
                            @"kml":@"GIS.png",
                            @"log":@"text.png",
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
                            @"mpeg":@"video.png",
                            @"mpg":@"video.png",
                            @"mrw":@"raw.png",
                            @"msi":@"executable.png",
                            @"nb":@"spreadsheet.png",
                            @"numbers":@"spreadsheet.png",
                            @"nef":@"raw.png",
                            @"obj":@"3D.png",
                            @"odp":@"powerpoint.png",
                            @"ods":@"spreadsheet.png",
                            @"odt":@"text.png",
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
                            @"ppj":@"Premiere.png",
                            @"pps":@"powerpoint.png",
                            @"ppt":@"powerpoint.png",
                            @"pptx":@"powerpoint.png",
                            @"prproj":@"Premiere.png",
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
    return dictionary;
}

@end
