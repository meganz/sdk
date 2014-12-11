/**
 * @file NodeViewModel.cs
 * @brief Class for the node view model.
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
using System.ComponentModel;
using System.Linq;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading.Tasks;
using mega;
using MegaApp.Converters;
using MegaApp.Extensions;
using MegaApp.Resources;

namespace MegaApp.Models
{
    /// <summary>
    /// ViewModel of the main MEGA datatype (MNode)
    /// </summary>
    public class NodeViewModel : BaseViewModel
    {
        private readonly MegaSDK _megaSdk;
        // Original MNode object from the MEGA SDK
        private readonly MNode _baseNode;
        // Offset DateTime value to calculate the correct creation and modification time
        private static readonly DateTime OriginalDateTime = new DateTime(1970, 1, 1, 0, 0, 0, 0);

        public NodeViewModel(MegaSDK megaSdk, MNode baseNode)
        {
            this._megaSdk = megaSdk;
            this._baseNode = baseNode;
            this.Name = baseNode.getName();
            this.Size = baseNode.getSize();
            this.CreationTime = ConvertDateToString(_baseNode.getCreationTime()).ToString("dd MMM yyyy");
            this.ModificationTime = ConvertDateToString(_baseNode.getModificationTime()).ToString("dd MMM yyyy");
            this.SizeAndSuffix = Size.ToStringAndSuffix();
            this.Type = baseNode.getType();
            this.NumberOfFiles = this.Type != MNodeType.TYPE_FOLDER ? null : String.Format("{0} {1}", this._megaSdk.getNumChildren(this._baseNode), UiResources.Files);

            if (this.Type == MNodeType.TYPE_FOLDER || this.Type == MNodeType.TYPE_FILE)                
                this.ThumbnailImageUri = new Uri((String)new FileTypeToImageConverter().Convert(this, null, null, null), UriKind.Relative);
        }

        #region Methods

        /// <summary>
        /// Convert the MEGA time to a C# DateTime object in local time
        /// </summary>
        /// <param name="time">MEGA time</param>
        /// <returns>DateTime object in local time</returns>
        private static DateTime ConvertDateToString(ulong time)
        {
            return OriginalDateTime.AddSeconds(time).ToLocalTime();
        }

        #endregion

        #region Properties

        public string Name { get; private set;}

        public ulong Size { get; private set; }

        public MNodeType Type { get; private set ; }

        public string CreationTime { get; private set; }

        public string ModificationTime { get; private set; }

        public string SizeAndSuffix { get; private set; }

        public string NumberOfFiles { get; private set; }

        public MNode GetBaseNode()
        {
            return this._baseNode;
        }

        public Uri ThumbnailImageUri { get; private set; }

        #endregion

        
    }
}
