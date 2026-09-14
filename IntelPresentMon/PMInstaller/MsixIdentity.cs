using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace PresentMon.Packaging
{
    public static class Identity
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct PackageId
        {
            public uint Reserved;
            public uint Architecture;
            public ulong Version;
            public string Name;
            public string Publisher;
            public string ResourceId;
            public string PublisherId;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern int PackageFamilyNameFromId(ref PackageId id, ref uint length, StringBuilder name);

        [DllImport("crypt32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CertGetCertificateContextProperty(IntPtr certificate, uint property, byte[] data, ref uint size);

        public static string FamilyName(string name, string publisher)
        {
            var id = new PackageId { Architecture = 9, Name = name, Publisher = publisher, ResourceId = "" };
            uint length = 0;
            int status = PackageFamilyNameFromId(ref id, ref length, null);
            if (status != 122) throw new Win32Exception(status);
            var result = new StringBuilder((int)length);
            status = PackageFamilyNameFromId(ref id, ref length, result);
            if (status != 0) throw new Win32Exception(status);
            return result.ToString();
        }

        public static string CertificateSignatureHash(IntPtr certificate)
        {
            const uint SignatureHashProperty = 15;
            uint size = 0;
            if (!CertGetCertificateContextProperty(certificate, SignatureHashProperty, null, ref size))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if (size != 32) throw new InvalidOperationException("SCCD requires a certificate with a SHA-256 signature hash.");
            var result = new byte[size];
            if (!CertGetCertificateContextProperty(certificate, SignatureHashProperty, result, ref size))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return BitConverter.ToString(result).Replace("-", "").ToLowerInvariant();
        }
    }
}
