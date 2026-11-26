using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace WxrInstaller
{
    /// <summary>One line of "wxrsetup --lines" output: Kind = INFO WARN ERROR OK FAIL NOTE PROGRESS DATA.</summary>
    public class SetupLine
    {
        public string Kind, Text, Key;   // DATA: Key + Text (value); PROGRESS: Key = percent, Text = what
        public bool IsProblem { get { return Kind == "FAIL" || Kind == "ERROR"; } }
    }

    public class SetupResult
    {
        public int ExitCode;
        public readonly List<SetupLine> Lines = new List<SetupLine>();
        public bool Ok { get { return ExitCode == 0; } }

        /// <summary>Value of a DATA line, or "".</summary>
        public string Data(string key)
        {
            for (int i = Lines.Count - 1; i >= 0; i--) if (Lines[i].Kind == "DATA" && Lines[i].Key == key) return Lines[i].Text;
            return "";
        }

        /// <summary>The lines of the given kinds, one per line of text.</summary>
        public string Text(params string[] kinds)
        {
            StringBuilder b = new StringBuilder();
            foreach (SetupLine l in Lines) if (Array.IndexOf(kinds, l.Kind) >= 0) b.AppendLine(l.Text);
            return b.ToString().TrimEnd();
        }
    }

    /// <summary>
    /// Runs wxrsetup.exe (next to this program) with --lines. All install logic and all checks live in wxrsetup; this class only
    /// starts it and parses its output. Run() blocks: call it from a background thread.
    /// </summary>
    public static class SetupTool
    {
        public static string BundleDir { get { return Application.StartupPath; } }
        public static string Exe { get { return Path.Combine(BundleDir, "wxrsetup.exe"); } }

        static Process current;    // the running wxrsetup, so Cancel can stop it

        /// <param name="root">install folder (--root), or null for wxrsetup's default</param>
        /// <param name="onLine">called on the background thread for every line as it arrives (may be null)</param>
        public static SetupResult Run(string root, Action<SetupLine> onLine, params string[] args)
        {
            SetupResult r = new SetupResult();
            List<string> all = new List<string>();
            all.Add("--lines");
            if (!string.IsNullOrEmpty(root)) { all.Add("--root"); all.Add(root); }
            all.AddRange(args);

            ProcessStartInfo si = new ProcessStartInfo(Exe, JoinArgs(all));
            si.UseShellExecute = false; si.CreateNoWindow = true;
            si.RedirectStandardOutput = true; si.RedirectStandardError = true;
            si.StandardOutputEncoding = Encoding.UTF8; si.StandardErrorEncoding = Encoding.UTF8;
            si.WorkingDirectory = BundleDir;
            using (Process p = new Process())
            {
                p.StartInfo = si;
                try { p.Start(); }
                catch (Exception ex)
                {
                    Add(r, onLine, "ERROR\tcannot start " + Exe + ": " + ex.Message); r.ExitCode = -1; return r;
                }
                current = p;
                p.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e) { if (!string.IsNullOrEmpty(e.Data)) lock (r) Add(r, onLine, "ERROR\t" + e.Data); };
                p.BeginErrorReadLine();
                string line;
                while ((line = p.StandardOutput.ReadLine()) != null) lock (r) Add(r, onLine, line);
                p.WaitForExit();
                r.ExitCode = p.ExitCode;
                current = null;
            }
            return r;
        }

        /// <summary>Stops the running wxrsetup (Cancel). wxrsetup records its state, so a later run continues or repairs.</summary>
        public static void Kill()
        {
            Process p = current;
            try { if (p != null && !p.HasExited) p.Kill(); } catch (InvalidOperationException) { } catch (System.ComponentModel.Win32Exception) { }
        }

        static void Add(SetupResult r, Action<SetupLine> onLine, string raw)
        {
            SetupLine l = new SetupLine();
            string[] f = raw.Split(new char[] { '\t' }, 3);
            l.Kind = f[0];
            if ((l.Kind == "DATA" || l.Kind == "PROGRESS") && f.Length == 3) { l.Key = f[1]; l.Text = f[2]; }
            else if (f.Length >= 2) l.Text = raw.Substring(f[0].Length + 1);
            else { l.Kind = "INFO"; l.Text = raw; }
            r.Lines.Add(l);
            if (onLine != null) onLine(l);
        }

        /// <summary>Windows command-line quoting (CommandLineToArgvW rules).</summary>
        static string JoinArgs(List<string> args)
        {
            StringBuilder b = new StringBuilder();
            foreach (string a in args)
            {
                if (b.Length > 0) b.Append(' ');
                if (a.Length > 0 && a.IndexOfAny(new char[] { ' ', '\t', '"' }) < 0) { b.Append(a); continue; }
                b.Append('"');
                int slashes = 0;
                foreach (char c in a)
                {
                    if (c == '\\') { slashes++; continue; }
                    if (c == '"') { b.Append('\\', slashes * 2 + 1); b.Append('"'); slashes = 0; continue; }
                    b.Append('\\', slashes); slashes = 0; b.Append(c);
                }
                b.Append('\\', slashes * 2);
                b.Append('"');
            }
            return b.ToString();
        }
    }
}
