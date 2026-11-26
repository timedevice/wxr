using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace WxrInstaller.Controls
{
    /// <summary>Windows' own command link, a Button created with BS_COMMANDLINK. Text = the main line, Note = the smaller line below.</summary>
    public class CommandLink : Button
    {
        const int BS_COMMANDLINK = 0x0000000E;
        const int BCM_SETNOTE = 0x1609;
        const int BCM_SETSHIELD = 0x160C;

        string note = "";
        bool shield;

        public CommandLink()
        {
            FlatStyle = FlatStyle.System;
        }

        protected override CreateParams CreateParams
        {
            get { CreateParams cp = base.CreateParams; cp.Style |= BS_COMMANDLINK; return cp; }
        }

        [Category("Appearance"), DefaultValue(""), Description("Second, smaller line under the main text.")]
        public string Note
        {
            get { return note; }
            set { note = value ?? ""; ApplyNote(); }
        }

        [Category("Appearance"), DefaultValue(false), Description("Show the UAC shield instead of the arrow.")]
        public bool Shield
        {
            get { return shield; }
            set { shield = value; ApplyShield(); }
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            ApplyNote();
            ApplyShield();
        }

        void ApplyNote() { if (IsHandleCreated) SendMessage(Handle, BCM_SETNOTE, IntPtr.Zero, note); }
        void ApplyShield() { if (IsHandleCreated) SendMessage(Handle, BCM_SETSHIELD, IntPtr.Zero, (IntPtr)(shield ? 1 : 0)); }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, string lParam);
        [DllImport("user32.dll")]
        static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);
    }
}
