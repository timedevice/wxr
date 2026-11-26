using System;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace WxrInstaller.Controls
{
    /// <summary>
    /// Windows Setup style step list: steps before Current get a check mark, Current gets an arrow and bold text
    /// (or a red cross when Failed), later steps are grey. Edit the steps in the Properties window (Steps).
    /// </summary>
    public class StepList : Control
    {
        string[] steps = new string[0];
        int current = -1;
        bool failed;
        int rowHeight = 28;

        public StepList()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
                     ControlStyles.UserPaint | ControlStyles.SupportsTransparentBackColor, true);
            BackColor = SystemColors.Window;
        }

        [Category("Appearance"), Description("The step texts, top to bottom.")]
        public string[] Steps
        {
            get { return steps; }
            set { steps = value ?? new string[0]; Invalidate(); }
        }

        [Category("Behavior"), DefaultValue(-1), Description("Index of the running step; -1 = not started, Steps.Length = all done.")]
        public int Current
        {
            get { return current; }
            set { current = value; Invalidate(); }
        }

        [Category("Behavior"), DefaultValue(false), Description("The current step failed: show a red cross instead of the arrow.")]
        public bool Failed
        {
            get { return failed; }
            set { failed = value; Invalidate(); }
        }

        [Category("Layout"), DefaultValue(28)]
        public int RowHeight
        {
            get { return rowHeight; }
            set { rowHeight = Math.Max(16, value); Invalidate(); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (Font bold = new Font(Font, FontStyle.Bold))
            {
                for (int i = 0; i < steps.Length; i++)
                {
                    int y = i * rowHeight;
                    Rectangle icon = new Rectangle(4, y + (rowHeight - 16) / 2, 16, 16);
                    bool done = i < current, active = i == current;
                    if (done) DrawCheck(g, icon);
                    else if (active && failed) DrawCross(g, icon);
                    else if (active) DrawArrow(g, icon);
                    Color c = done || active ? ForeColor : SystemColors.GrayText;
                    TextRenderer.DrawText(g, steps[i], active ? bold : Font, new Rectangle(28, y, Width - 28, rowHeight), c,
                                          TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
                }
            }
        }

        static void DrawCheck(Graphics g, Rectangle r)
        {
            using (Pen p = new Pen(Color.FromArgb(0, 150, 50), 2.4f))
            {
                p.StartCap = p.EndCap = LineCap.Round; p.LineJoin = LineJoin.Round;
                g.DrawLines(p, new Point[] { new Point(r.X + 3, r.Y + 8), new Point(r.X + 7, r.Y + 12), new Point(r.X + 13, r.Y + 4) });
            }
        }

        static void DrawCross(Graphics g, Rectangle r)
        {
            using (Pen p = new Pen(Color.FromArgb(200, 30, 30), 2.4f))
            {
                p.StartCap = p.EndCap = LineCap.Round;
                g.DrawLine(p, r.X + 4, r.Y + 4, r.X + 12, r.Y + 12);
                g.DrawLine(p, r.X + 12, r.Y + 4, r.X + 4, r.Y + 12);
            }
        }

        static void DrawArrow(Graphics g, Rectangle r)
        {
            using (Brush b = new SolidBrush(SystemColors.Highlight))
                g.FillPolygon(b, new Point[] { new Point(r.X + 5, r.Y + 3), new Point(r.X + 12, r.Y + 8), new Point(r.X + 5, r.Y + 13) });
        }
    }
}
