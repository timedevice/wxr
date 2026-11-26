using System;
using System.Windows.Forms;

namespace WxrInstaller
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application. Arguments: /modify, /uninstall, /root "&lt;folder&gt;" (see Main).
        /// </summary>
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Main(args));
        }
    }
}
