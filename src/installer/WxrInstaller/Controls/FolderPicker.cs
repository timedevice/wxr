using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace WxrInstaller.Controls
{
    /// <summary>
    /// The Vista+ folder picker (the Explorer-style dialog, IFileOpenDialog with FOS_PICKFOLDERS) instead of .NET 3.5's
    /// old tree-view FolderBrowserDialog. Falls back to FolderBrowserDialog if the COM dialog cannot be created.
    /// </summary>
    public static class FolderPicker
    {
        /// <returns>The chosen folder, or null if the user cancelled.</returns>
        public static string Show(IWin32Window owner, string title, string initialFolder)
        {
            IFileDialog dlg = null;
            try { dlg = (IFileDialog)new FileOpenDialogRCW(); }
            catch (COMException) { }
            if (dlg == null) return ShowOld(owner, title, initialFolder);
            try
            {
                uint opts;
                dlg.GetOptions(out opts);
                dlg.SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
                if (!string.IsNullOrEmpty(title)) dlg.SetTitle(title);
                string start = NearestExisting(initialFolder);
                if (start != null)
                {
                    IShellItem folder; Guid iid = IID_IShellItem;
                    if (SHCreateItemFromParsingName(start, IntPtr.Zero, ref iid, out folder) == 0) dlg.SetFolder(folder);
                }
                if (dlg.Show(owner == null ? IntPtr.Zero : owner.Handle) != 0) return null;   // cancelled (or failed)
                IShellItem result; IntPtr p;
                dlg.GetResult(out result);
                result.GetDisplayName(SIGDN_FILESYSPATH, out p);
                try { return Marshal.PtrToStringUni(p); } finally { Marshal.FreeCoTaskMem(p); }
            }
            finally { Marshal.ReleaseComObject(dlg); }
        }

        static string ShowOld(IWin32Window owner, string title, string initialFolder)
        {
            using (FolderBrowserDialog f = new FolderBrowserDialog())
            {
                f.Description = title; f.SelectedPath = NearestExisting(initialFolder) ?? "";
                return f.ShowDialog(owner) == DialogResult.OK ? f.SelectedPath : null;
            }
        }

        /// <summary>The folder itself if it exists, else its closest existing parent (C:\WXR not created yet -> C:\).</summary>
        static string NearestExisting(string path)
        {
            try
            {
                while (!string.IsNullOrEmpty(path) && !System.IO.Directory.Exists(path)) path = System.IO.Path.GetDirectoryName(path);
                return string.IsNullOrEmpty(path) ? null : path;
            }
            catch (ArgumentException) { return null; }
        }

        const uint FOS_PICKFOLDERS = 0x20, FOS_FORCEFILESYSTEM = 0x40, FOS_PATHMUSTEXIST = 0x800;
        const uint SIGDN_FILESYSPATH = 0x80058000;
        static readonly Guid IID_IShellItem = new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");

        [ComImport, Guid("DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7")]
        class FileOpenDialogRCW { }

        [ComImport, Guid("42F85136-DB7E-439C-85F1-E4075D135FC8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IFileDialog
        {
            [PreserveSig] int Show(IntPtr parent);                       // IModalWindow
            void SetFileTypes(uint count, IntPtr specs);
            void SetFileTypeIndex(uint index);
            void GetFileTypeIndex(out uint index);
            void Advise(IntPtr events, out uint cookie);
            void Unadvise(uint cookie);
            void SetOptions(uint fos);
            void GetOptions(out uint fos);
            void SetDefaultFolder(IShellItem item);
            void SetFolder(IShellItem item);
            void GetFolder(out IShellItem item);
            void GetCurrentSelection(out IShellItem item);
            void SetFileName([MarshalAs(UnmanagedType.LPWStr)] string name);
            void GetFileName([MarshalAs(UnmanagedType.LPWStr)] out string name);
            void SetTitle([MarshalAs(UnmanagedType.LPWStr)] string title);
            void SetOkButtonLabel([MarshalAs(UnmanagedType.LPWStr)] string text);
            void SetFileNameLabel([MarshalAs(UnmanagedType.LPWStr)] string label);
            void GetResult(out IShellItem item);
            void AddPlace(IShellItem item, int fdap);
            void SetDefaultExtension([MarshalAs(UnmanagedType.LPWStr)] string ext);
            void Close(int hr);
            void SetClientGuid(ref Guid guid);
            void ClearClientData();
            void SetFilter(IntPtr filter);
        }

        [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IShellItem
        {
            void BindToHandler(IntPtr bc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
            void GetParent(out IShellItem parent);
            void GetDisplayName(uint sigdn, out IntPtr name);
            void GetAttributes(uint mask, out uint attribs);
            void Compare(IShellItem other, uint hint, out int order);
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        static extern int SHCreateItemFromParsingName(string path, IntPtr bc, ref Guid riid, out IShellItem item);
    }
}
