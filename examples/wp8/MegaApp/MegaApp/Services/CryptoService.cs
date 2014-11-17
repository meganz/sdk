using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace MegaApp.Services
{
    static class CryptoService
    {
        public static string EncryptValue(string value)
        {
            var valueBytes = Encoding.UTF8.GetBytes(value);
            var protectedBytes = ProtectedData.Protect(valueBytes, null);
            return Encoding.UTF8.GetString(protectedBytes, 0, protectedBytes.Length);
        }

        public static string DecryptValue(string value)
        {
            var protectedBytes = Encoding.UTF8.GetBytes(value);
            var valueBytes = ProtectedData.Unprotect(protectedBytes, null);
            return Encoding.UTF8.GetString(valueBytes, 0, valueBytes.Length);
        }
    }
}
