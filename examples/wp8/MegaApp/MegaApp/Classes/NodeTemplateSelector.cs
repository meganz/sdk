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
