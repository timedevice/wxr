using System;
using System.Windows.Forms;

namespace WxrInstaller.Controls
{
    /// <summary>
    /// A TabControl used as a page stack: the tab strip is visible in the Designer (click a tab to edit that page)
    /// but takes no space at run time. Switch pages from code with SelectedTab / SelectedIndex.
    /// </summary>
    public class WizardPages : TabControl
    {
        const int TCM_ADJUSTRECT = 0x1328;

        protected override void WndProc(ref Message m)
        {
            // at run time: report the display area as the whole control, so no tabs and no border are drawn
            if (m.Msg == TCM_ADJUSTRECT && !DesignMode) { m.Result = (IntPtr)1; return; }
            base.WndProc(ref m);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            // no Ctrl+Tab / Ctrl+PageUp/PageDown page switching: only the wizard's own buttons move between pages
            if (e.Control && (e.KeyCode == Keys.Tab || e.KeyCode == Keys.PageUp || e.KeyCode == Keys.PageDown)) { e.Handled = true; return; }
            base.OnKeyDown(e);
        }
    }
}
