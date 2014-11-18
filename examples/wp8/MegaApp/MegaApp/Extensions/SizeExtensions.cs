using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MegaApp.Extensions
{
    static class SizeExtensions
    {
        private static readonly string[] SizeSuffixes = { "bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

        public static string ToStringAndSuffix(this UInt64 value)
        {
            if (value == 0) { return "0.0 bytes"; }

            int mag = (int)Math.Log(value, 1024);
            decimal adjustedSize = (decimal)value / (1L << (mag * 10));

            return String.Format("{0:n2} {1}", adjustedSize, SizeSuffixes[mag]);
        }
    }
}
