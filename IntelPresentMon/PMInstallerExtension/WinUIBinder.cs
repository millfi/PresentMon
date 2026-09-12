using System;
using System.Globalization;
using Microsoft.Tools.WindowsInstallerXml;

namespace pm_installer
{
    public sealed class pmi_winui_binder_extension : BinderExtension
    {
        public override void DatabaseFinalize(Output output)
        {
            var files = output.Tables["File"];
            if (files == null)
            {
                return;
            }
            foreach (FileRow file in files.Rows)
            {
                var language = NormalizeLanguage(file.File, file.Source, file.Language);
                if (language != file.Language)
                {
                    file.Language = language;
                    Console.WriteLine("Normalized Windows App SDK MSI language metadata: " + file.Source);
                }
            }
        }

        internal static string NormalizeLanguage(string fileId, string source, string language)
        {
            if (string.IsNullOrEmpty(source) || string.IsNullOrEmpty(language))
            {
                return language;
            }

            // The IDs are generated from relative paths by harvest-winui.ps1.
            // Restrict this workaround to the SDK's two multilingual XAML DLLs
            // and their three MUI locales that WiX 3 ICE03 cannot represent.
            // Only MSI replacement metadata becomes neutral; resource bytes,
            // version numbers, hashes, and every other file remain unchanged.
            string relativePath;
            string localeLanguage = null;
            switch (fileId)
            {
                case "winui_file_C60D09577FD5F55E1E55DFB930622EB8":
                    relativePath = "Microsoft.ui.xaml.dll";
                    break;
                case "winui_file_C104A8CBE0118ED04391D96F6C25DFB3":
                    relativePath = "Microsoft.UI.Xaml.Phone.dll";
                    break;
                case "winui_file_F218415B4FE94603ACB3A9D39C214D6E":
                    relativePath = @"gd-gb\Microsoft.ui.xaml.dll.mui";
                    localeLanguage = "1169";
                    break;
                case "winui_file_93FA4D3918E9894377430E98473D9396":
                    relativePath = @"gd-gb\Microsoft.UI.Xaml.Phone.dll.mui";
                    localeLanguage = "1169";
                    break;
                case "winui_file_88EEE8F28EA7AC266255111EA4DA2F7E":
                    relativePath = @"mi-NZ\Microsoft.ui.xaml.dll.mui";
                    localeLanguage = "1153";
                    break;
                case "winui_file_1DC483124C37819A8537A0D5B943186B":
                    relativePath = @"mi-NZ\Microsoft.UI.Xaml.Phone.dll.mui";
                    localeLanguage = "1153";
                    break;
                case "winui_file_3BB7F795433A6F85072064FCEB771E7C":
                    relativePath = @"ug-CN\Microsoft.ui.xaml.dll.mui";
                    localeLanguage = "1152";
                    break;
                case "winui_file_74AC977EF38584A8EBCE9F59279205BF":
                    relativePath = @"ug-CN\Microsoft.UI.Xaml.Phone.dll.mui";
                    localeLanguage = "1152";
                    break;
                default:
                    return language;
            }

            if (!source.Replace('/', '\\').EndsWith(@"\ui\" + relativePath, StringComparison.OrdinalIgnoreCase))
            {
                return language;
            }
            if (localeLanguage != null)
            {
                return language == localeLanguage ? "0" : language;
            }
            if (language.Length <= 20)
            {
                return language;
            }
            foreach (var item in language.Split(','))
            {
                if (!ushort.TryParse(item, NumberStyles.None, CultureInfo.InvariantCulture, out _))
                {
                    return language;
                }
            }
            return "0";
        }
    }
}
