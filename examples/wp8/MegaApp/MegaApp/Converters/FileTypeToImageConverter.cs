/**
 * @file FileTypeToImageConverter.cs
 * @brief Class for convert the file type to a image icon.
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

using System.IO;
using System;
using System.Globalization;
using System.Windows.Data;
using mega;
using MegaApp.Models;

namespace MegaApp.Converters
{
    public class FileTypeToImageConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value == null) return null;

            var node = (NodeViewModel)value;
            switch (node.Type)
            {
                case MNodeType.TYPE_FOLDER:
                    {
                        return "/Assets/FileTypes/folder.png";
                    }
                case MNodeType.TYPE_FILE:
                    {
                        var fileExtension = Path.GetExtension(node.Name);
                        if (fileExtension == null) return "/Assets/FileTypes/file.png";
                        switch (fileExtension.ToLower())
                        {
                            case ".accdb":
                                {
                                    return "/Assets/FileTypes/accdb.png";
                                }
                            case ".bmp":
                                {
                                    return "/Assets/FileTypes/bmp.png";
                                }
                            case ".doc":
                            case ".docx":
                                {
                                    return "/Assets/FileTypes/doc.png";
                                }
                            case ".eps":
                                {
                                    return "/Assets/FileTypes/eps.png";
                                }
                            case ".gif":
                                {
                                    return "/Assets/FileTypes/gif.png";
                                }
                            case ".ico":
                                {
                                    return "/Assets/FileTypes/ico.png";
                                }
                            case ".jpg":
                            case ".jpeg":
                                {
                                    return "/Assets/FileTypes/jpg.png";
                                }
                            case ".mp3":
                                {
                                    return "/Assets/FileTypes/mp3.png";
                                }
                            case ".pdf":
                                {
                                    return "/Assets/FileTypes/pdf.png";
                                }
                            case ".png":
                                {
                                    return "/Assets/FileTypes/png.png";
                                }
                            case ".ppt":
                            case ".pptx":
                                {
                                    return "/Assets/FileTypes/ppt.png";
                                }
                            case ".swf":
                                {
                                    return "/Assets/FileTypes/swf.png";
                                }
                            case ".tga":
                                {
                                    return "/Assets/FileTypes/tga.png";
                                }
                            case ".tiff":
                                {
                                    return "/Assets/FileTypes/tiff.png";
                                }
                            case ".txt":
                                {
                                    return "/Assets/FileTypes/txt.png";
                                }
                            case ".wav":
                                {
                                    return "/Assets/FileTypes/wav.png";
                                }
                            case ".xls":
                            case ".xlsx":
                                {
                                    return "/Assets/FileTypes/xls.png";
                                }
                            case ".zip":
                                {
                                    return "/Assets/FileTypes/zip.png";
                                }
                            default:
                                {
                                    return "/Assets/Images/file.png";
                                }
                        }
                    }
                default:
                    {
                        return null;
                    }
            }

            
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
