using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Security.Cryptography;
using mega;

namespace MegaApp.MegaApi
{
    class MegaRandomNumberProvider : MRandomNumberProvider
    {
        private static readonly RNGCryptoServiceProvider RngCsp = new RNGCryptoServiceProvider();

        public virtual void GenerateRandomBlock(byte[] value)
        {
            RngCsp.GetBytes(value);
        }
    }
}
