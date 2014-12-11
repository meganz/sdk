/**
 * @file NodeTemplateSelector.cs
 * @brief Class for describe the visual structure of a data object.
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

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using mega;
using MegaApp.Models;

namespace MegaApp.Classes
{
    public class NodeTemplateSelector : DataTemplate
    {
        public DataTemplate FolderItemTemplate { get; set; }
        public DataTemplate FileItemTemplate { get; set; }

        public DataTemplate SelectTemplate(object item, DependencyObject container)
        {
            var nodeViewModel = item as NodeViewModel;

            if (nodeViewModel == null) return null;

            switch (nodeViewModel.Type)
            {
                case MNodeType.TYPE_FOLDER:
                {
                    return FolderItemTemplate;                    
                }
                default:
                {
                    return FileItemTemplate;                    
                }
            }
        }
        
    }
}
