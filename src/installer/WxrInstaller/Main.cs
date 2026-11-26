using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using WxrInstaller.Controls;
using Timer = System.Windows.Forms.Timer;

namespace WxrInstaller
{
    /// <summary>
    /// The setup wizard. Every check and every install step is a wxrsetup.exe call (SetupTool); this form only decides the order,
    /// shows the results and keeps the user from going on while something is wrong.
    /// Command line (Programs and Features): /modify = Repair / Uninstall page, /uninstall = uninstall, /root "C:\WXR" = which install
    /// (for a new install: the folder Custom starts with), /iso "file" = the ISO field filled in.
    /// </summary>
    public partial class Main : Form
    {
        enum Mode { Express, Custom }
        enum Flow { Install, Repair, Uninstall }

        const string DefaultRoot = @"C:\WXR";

        // component list (same order as listComponents.Items): description, default, name for "wxrsetup components"
        // (wxrsetup installs and removes them and records them in environment.ini [components])
        static readonly string[] ComponentInfo = {
            "Apps built on Windows' own XAML (Windows.UI.Xaml) and WinUI 2, which most Store apps use: Calculator, Clock, " +
            "Media Player, Microsoft Store and others. Adds about 30 MB of Windows 10 files from the ISO.\r\n\r\n" +
            "Apps built on WinUI 3 (Windows App SDK) are always supported and do not need this: their runtime is installed " +
            "with the first app that uses it.",

            "PC games built with Microsoft's Game Development Kit (GDK).\r\n\r\n" +
            "Setup downloads Microsoft's Gaming Services package (about 200 MB, internet needed) and keeps only the three game " +
            "runtime files from it. Each game's own GameInput redistributable is set up when the game is installed.\r\n\r\n" +
            "Not working yet: installing games through the Store, game licences (a game that checks ownership runs as a " +
            "trial) and game controllers. Games are added from an installed copy, for example the XboxGames folder of a " +
            "Windows 10 PC, with wxrpkg add.",

            "Experimental. Lets the Microsoft Store use your Microsoft account, so it can show your library and install " +
            "free apps and apps your account owns (some free apps still fail, for reasons not known yet).\r\n\r\n" +
            "Sign in with WXR Sign-in in the Start menu: it shows a code that you enter at microsoft.com/link on any " +
            "device. Your password is entered on Microsoft's page only and never passes through WXR; the sign-in is " +
            "stored encrypted for your Windows user. No background service is installed.\r\n\r\n" +
            "Not working yet: buying paid apps and showing the signed-in account in the Store.\r\n\r\n" +
            "Without it, the Store works signed out and free apps still install without an account (wxrpkg store).",
        };
        static readonly bool[] ComponentDefault = { true, false, false };
        static readonly string[] ComponentKey = { "uwp", "gdk", "msa" };
        const int CompUwp = 0, CompGdk = 1, CompMsa = 2;

        static readonly string[] InstallSteps = { "Checking this computer", "Reading the Windows 10 ISO", "Copying Windows 10 files",
                                                  "Installing WXR", "Registering", "Setting up components", "Finishing up" };
        static readonly string[] UninstallSteps = { "Stopping WXR", "Removing WXR, its Windows 10 files and its apps" };

        Mode mode = Mode.Express;
        Flow flow = Flow.Install;
        readonly bool argModify, argUninstall;

        readonly string installedRoot;      // the installation Repair / Uninstall work on: decided once in the constructor, never changed
        string workRoot;                    // the folder the running install / repair / uninstall works on, set when it starts
        string checkedFolder;               // the folder the last successful folder check was for (Install needs it to match the page)

        // results of the checks on the ISO / folder page
        bool isoOk, folderOk, anyBuild, imageExtracted, rootExisting, uwpFiles;   // uwpFiles: the folder has Windows' XAML files
        int isoGen, folderGen;               // drop results of checks that were overtaken by a newer edit
        readonly Timer isoTimer = new Timer(), folderTimer = new Timer();

        volatile bool installing, cancelRequested;

        public Main(string[] args)
        {
            InitializeComponent();
            string rootArg = null;
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i].ToLowerInvariant();
                if (a == "/modify") argModify = true;
                else if (a == "/uninstall") argUninstall = true;
                else if (a == "/root" && i + 1 < args.Length) rootArg = args[++i];
                else if (a == "/iso" && i + 1 < args.Length) textIso.Text = args[++i];
            }
            if (argModify || argUninstall)
                installedRoot = rootArg != null ? (IsWxrInstall(rootArg) ? Path.GetFullPath(rootArg).TrimEnd('\\') : null) : FindInstalledRoot();   // never a default
            else if (rootArg != null) textFolder.Text = rootArg;   // new install: Custom starts with this folder (no check yet: not on that page)
            for (int i = 0; i < ComponentDefault.Length && i < listComponents.Items.Count; i++)
                listComponents.SetItemChecked(i, ComponentDefault[i]);
            if (listComponents.Items.Count > 0) listComponents.SelectedIndex = 0;   // description box is never empty
            isoTimer.Interval = folderTimer.Interval = 500;
            isoTimer.Tick += delegate { isoTimer.Stop(); CheckIso(); };
            folderTimer.Tick += delegate { folderTimer.Stop(); CheckFolder(); };
            linkFaq.LinkClicked += delegate { OpenUrl("https://github.com/"); };       // TODO: or maybe not hyperlinks, possibly write full FAQ in installer later
            linkGetApps.LinkClicked += delegate { OpenUrl("https://github.com/"); };
            linkGetIso.LinkClicked += delegate { OpenUrl("https://www.microsoft.com/software-download/windows10"); };
            labelCheckNote.Text = "Checking this computer...";
            linkExpress.Enabled = linkCustom.Enabled = false;
            ShowPage(argModify || argUninstall ? pageMaintain : pageStart);
            Shown += Main_Shown;
        }

        static bool HasArg(string[] args, string a) { foreach (string s in args) if (s.ToLowerInvariant() == a) return true; return false; }

        void Main_Shown(object sender, EventArgs e)
        {
            if ((argModify || argUninstall) && installedRoot == null)
            {
                MessageBox.Show(this, "This setup program cannot tell which WXR installation it belongs to, so it does not change or remove anything.\n\n" +
                                "Run the setup program inside the installation's Setup folder, or pass /root <folder>.", Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close(); return;
            }
            labelMaintain.Text = "Installed in " + installedRoot;
            if (argUninstall) { BeginUninstall(); return; }
            RunInBackground(delegate
            {
                SetupResult r = SetupTool.Run(null, null, "check", "system");
                Ui(delegate { SystemChecked(r); });
            });
        }

        void SystemChecked(SetupResult r)
        {
            if (!r.Ok)
            {
                labelBlocked.Text = r.Text("FAIL", "ERROR") + "\n\nFix the problem above, then run setup again.";
                ShowPage(pageBlocked);
                return;
            }
            labelCheckNote.Text = r.Text("WARN");
            linkExpress.Enabled = linkCustom.Enabled = true;
            if (textIso.Text.Length == 0) textIso.Text = GuessIso();
        }

        // navigation

        void ShowPage(TabPage page)
        {
            wizard.SelectedTab = page;
            UpdateButtons();
        }

        void UpdateButtons()
        {
            TabPage p = wizard.SelectedTab;
            bool paths = p == pagePaths, comps = p == pageComponents;
            bool isoMissing = comps && NeedIso() && !isoOk;          // Repair: the ISO page comes next; Custom: it was skipped too early
            buttonBack.Visible = paths || comps;
            buttonNext.Visible = paths || comps;
            buttonNext.Text = (paths && (mode == Mode.Express || flow == Flow.Repair)) || (comps && !(flow == Flow.Repair && isoMissing)) ? "Install" : "Next >";
            buttonNext.Enabled = paths ? PathsOk() : comps ? flow == Flow.Repair || !isoMissing : true;   // no component is required
            buttonCancel.Text = p == pageDone || p == pageBlocked ? "Close" : "Cancel";
            buttonCancel.Visible = true;
            AcceptButton = buttonNext.Visible ? buttonNext : null;
        }

        void linkExpress_Click(object sender, EventArgs e)
        {
            mode = Mode.Express; flow = Flow.Install;
            SetFolderControlsVisible(false);
            titleLabel2.Text = "Select an ISO";
            ShowPage(pagePaths);
            CheckFolder();
        }

        void linkCustom_Click(object sender, EventArgs e)
        {
            mode = Mode.Custom; flow = Flow.Install;
            SetFolderControlsVisible(true);
            titleLabel2.Text = "Select an ISO and a location";
            ShowPage(pagePaths);
            CheckFolder();
        }

        void SetFolderControlsVisible(bool v)
        {
            labelFolder.Visible = textFolder.Visible = buttonFolderBrowse.Visible = v;   // the status line stays: Express shows C:\WXR's result there
        }

        void buttonBack_Click(object sender, EventArgs e)
        {
            if (wizard.SelectedTab == pagePaths) ShowPage(flow == Flow.Repair ? pageComponents : pageStart);
            else if (wizard.SelectedTab == pageComponents) ShowPage(flow == Flow.Repair ? pageMaintain : pagePaths);
        }

        void buttonNext_Click(object sender, EventArgs e)
        {
            if (wizard.SelectedTab == pagePaths)
            {
                if (mode == Mode.Express || flow == Flow.Repair) StartInstall();
                else ShowPage(pageComponents);
            }
            else if (wizard.SelectedTab == pageComponents)
            {
                if (flow == Flow.Repair && NeedIso() && !isoOk)
                {   // Windows 10 files are missing (never extracted, or UWP was just ticked): ask for the ISO, folder fixed
                    SetFolderControlsVisible(false);
                    titleLabel2.Text = "Select an ISO";
                    ShowPage(pagePaths);
                    CheckFolder();
                }
                else StartInstall();
            }
        }

        bool ComponentWanted(int i)
        {
            return mode == Mode.Express && flow == Flow.Install ? ComponentDefault[i] : i < listComponents.Items.Count && listComponents.GetItemChecked(i);
        }

        bool NeedIso() { return !imageExtracted || (ComponentWanted(CompUwp) && !uwpFiles); }

        void buttonCancel_Click(object sender, EventArgs e) { Close(); }

        void Main_FormClosing(object sender, FormClosingEventArgs e)
        {
            if (!installing) return;
            if (MessageBox.Show(this, "Setup is still running. Stop it?\n\nYou can run setup again later; it repairs or continues the installation.",
                                Text, MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes)
            { e.Cancel = true; return; }
            cancelRequested = true;
            SetupTool.Kill();
        }

        // maintenance ( Programs and Features Change option)

        void linkRepair_Click(object sender, EventArgs e)
        {
            flow = Flow.Repair; mode = Mode.Custom;
            string dir = installedRoot;
            textFolder.Text = dir;
            linkRepair.Enabled = false;
            RunInBackground(delegate
            {
                SetupResult r = SetupTool.Run(dir, null, "check", "root");
                Ui(delegate
                {
                    linkRepair.Enabled = true;
                    imageExtracted = r.Data("image") == "extracted";
                    uwpFiles = r.Data("uwpfiles") == "1";
                    for (int i = 0; i < ComponentKey.Length && i < listComponents.Items.Count; i++)
                        listComponents.SetItemChecked(i, r.Data("comp." + ComponentKey[i]) == "1");
                    titleLabel3.Text = "Select components";
                    ShowPage(pageComponents);
                });
            });
        }

        void linkUninstall_Click(object sender, EventArgs e) { BeginUninstall(); }

        // ------------------------------------------------------------------ ISO + folder page

        void buttonIsoBrowse_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog d = new OpenFileDialog())
            {
                d.Title = "Choose the Windows 10 ISO";
                d.Filter = "Windows 10 image (*.iso;*.esd;*.wim)|*.iso;*.esd;*.wim|All files (*.*)|*.*";
                if (File.Exists(textIso.Text)) d.FileName = textIso.Text;
                if (d.ShowDialog(this) == DialogResult.OK) textIso.Text = d.FileName;
            }
        }

        void buttonFolderBrowse_Click(object sender, EventArgs e)
        {
            string f = FolderPicker.Show(this, "Choose where to install WXR", textFolder.Text);
            if (f != null) textFolder.Text = f;
        }

        void textIso_TextChanged(object sender, EventArgs e)
        {
            isoOk = false; isoGen++;
            SetStatus(labelIsoStatus, 0, textIso.Text.Trim().Length > 0 ? "Checking..." : "");
            isoTimer.Stop(); isoTimer.Start();
            UpdateButtons();
        }

        void textFolder_TextChanged(object sender, EventArgs e)
        {
            folderOk = false; folderGen++; checkedFolder = null;
            if (wizard.SelectedTab != pagePaths) return;            // filled in from code on another page: checked when the page is shown
            SetStatus(labelFolderStatus, 0, "Checking...");
            folderTimer.Stop(); folderTimer.Start();
            UpdateButtons();
        }

        /// <summary>The folder an install started now would use. Express: C:\WXR; Repair: the installation; Custom: the field.</summary>
        string TargetFolder()
        {
            if (flow == Flow.Repair) return installedRoot;
            if (mode == Mode.Express) return DefaultRoot;
            return textFolder.Text.Trim();
        }

        static bool SameFolder(string a, string b)
        {
            if (string.IsNullOrEmpty(a) || string.IsNullOrEmpty(b)) return false;
            try { return string.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase); }
            catch (ArgumentException) { return false; } catch (NotSupportedException) { return false; }
        }

        // Install only when the folder that was checked is still the folder on the page
        bool PathsOk() { return folderOk && SameFolder(checkedFolder, TargetFolder()) && (isoOk || !NeedIso()); }

        void CheckIso()
        {
            string iso = textIso.Text.Trim(); int gen = ++isoGen;
            if (iso.Length == 0)
            {
                SetStatus(labelIsoStatus, 0, !NeedIso() ? "Not needed: the install folder already has the Windows 10 files." : "");
                UpdateButtons(); return;
            }
            RunInBackground(delegate
            {
                SetupResult r = SetupTool.Run(null, null, "inspect", iso);
                Ui(delegate
                {
                    if (gen != isoGen) return;                       // the text changed meanwhile
                    isoOk = r.Ok; anyBuild = r.Data("anybuild") == "1";
                    string bad = r.Text("FAIL", "ERROR"), warn = r.Text("WARN");
                    if (!r.Ok) SetStatus(labelIsoStatus, -1, bad.Length > 0 ? bad : "This file cannot be used.");
                    else if (warn.Length > 0) SetStatus(labelIsoStatus, 0, r.Text("OK") + "\n" + warn);
                    else SetStatus(labelIsoStatus, 1, r.Text("OK"));
                    UpdateButtons();
                });
            });
        }

        void CheckFolder()
        {
            string dir = TargetFolder() ?? "";
            int gen = ++folderGen; checkedFolder = null;
            if (dir.Length == 0) { folderOk = false; SetStatus(labelFolderStatus, -1, "Choose a folder."); UpdateButtons(); return; }
            if (!Path.IsPathRooted(dir)) { folderOk = false; SetStatus(labelFolderStatus, -1, "Enter a full path, for example C:\\WXR."); UpdateButtons(); return; }
            RunInBackground(delegate
            {
                SetupResult r = SetupTool.Run(dir, null, "check", "root");
                Ui(delegate
                {
                    if (gen != folderGen) return;
                    folderOk = r.Ok;
                    checkedFolder = r.Ok ? dir : null;                // only a record of what was checked: never the target of anything
                    rootExisting = r.Data("root") == "existing";
                    imageExtracted = rootExisting && r.Data("image") == "extracted";
                    uwpFiles = rootExisting && r.Data("uwpfiles") == "1";
                    string prefix = mode == Mode.Express ? dir + ": " : "";
                    string bad = r.Text("FAIL", "ERROR"), warn = r.Text("WARN");
                    if (!r.Ok) SetStatus(labelFolderStatus, -1, prefix + (bad.Length > 0 ? bad : "This folder cannot be used.") +
                                                                (mode == Mode.Express ? "\nUse Custom install to choose another folder." : ""));
                    else if (warn.Length > 0) SetStatus(labelFolderStatus, 0, prefix + warn);
                    else SetStatus(labelFolderStatus, 1, prefix + FirstLine(r.Text("OK")));
                    if (textIso.Text.Trim().Length == 0) CheckIso();   // "not needed" note when the folder already has the files
                    UpdateButtons();
                });
            });
        }

        /// <summary>state: 1 ok, 0 neutral, -1 problem</summary>
        static void SetStatus(Label l, int state, string text)
        {
            l.Text = text;
            l.ForeColor = state > 0 ? Color.FromArgb(0, 120, 40) : state < 0 ? Color.FromArgb(190, 30, 30) : SystemColors.GrayText;
        }

        static string FirstLine(string s) { int i = s.IndexOf('\n'); return (i < 0 ? s : s.Substring(0, i)).TrimEnd('\r'); }

        /// <summary>A Windows ISO in Downloads or on the Desktop (newest first), so most users never need Browse. wxrsetup inspect then checks it.</summary>
        static string GuessIso()
        {
            string home = Environment.GetEnvironmentVariable("USERPROFILE") ?? "";
            string[] dirs = { Path.Combine(home, "Downloads"), Path.Combine(home, "Desktop") };
            FileInfo best = null;
            foreach (string d in dirs)
            {
                try
                {
                    if (!Directory.Exists(d)) continue;
                    foreach (FileInfo f in new DirectoryInfo(d).GetFiles("*.iso"))
                        if (f.Name.ToLowerInvariant().Contains("win") && (best == null || f.LastWriteTime > best.LastWriteTime)) best = f;
                }
                catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
            return best == null ? "" : best.FullName;
        }

        // ------------------------------------------------------------------ components page

        void listComponents_SelectedIndexChanged(object sender, EventArgs e)
        {
            int i = listComponents.SelectedIndex;
            textComponentInfo.Text = i >= 0 && i < ComponentInfo.Length ? ComponentInfo[i] : "";
        }

        void listComponents_ItemCheck(object sender, ItemCheckEventArgs e)
        {
            // CheckedItems updates only after this event: re-evaluate once it has
            if (IsHandleCreated) BeginInvoke(new MethodInvoker(UpdateButtons));   // not while the constructor sets the defaults
        }

        // ------------------------------------------------------------------ install / repair

        void StartInstall()
        {
            string iso = textIso.Text.Trim(), dir = TargetFolder(), bundle = SetupTool.BundleDir;
            if (flow != Flow.Repair && !SameFolder(checkedFolder, dir)) { CheckFolder(); return; }   // not checked yet: Install stays off until it is
            workRoot = dir;
            bool any = anyBuild, needIso = NeedIso();
            bool[] comps = new bool[ComponentKey.Length];
            List<string> chosen = new List<string>();
            for (int i = 0; i < comps.Length; i++) { comps[i] = ComponentWanted(i); if (comps[i]) chosen.Add(ComponentKey[i]); }
            string compList = chosen.Count > 0 ? string.Join(",", chosen.ToArray()) : "none";

            BeginSteps(flow == Flow.Repair ? "Repairing" : "Installing", InstallSteps);
            RunInBackground(delegate
            {
                SetupResult r;
                // 0 checks: the computer and the folder, once more right before anything is written
                if (!Step(0, r = SetupTool.Run(dir, OnLine, "check"))) return;
                // 1 the ISO (not needed when the folder has every Windows 10 file the components need)
                if (needIso && !Step(1, r = SetupTool.Run(null, OnLine, "inspect", iso))) return;
                // 2 the Windows 10 files (UWP's XAML in the same pass when it is chosen)
                Ui(delegate { stepList.Current = 2; });
                if (!Step(2, r = SetupTool.Run(dir, OnLine, "create"), false)) return;
                string image = SetupTool.Run(dir, null, "check", "root").Data("image");
                if (image == "incomplete" && !Step(2, SetupTool.Run(dir, OnLine, "--yes", "purge", "image"), false)) return;   // an earlier run was stopped half-way
                if (image != "extracted")
                {
                    List<string> a = new List<string>(new string[] { "--components", comps[CompUwp] ? "uwp" : "none", "extract", iso });
                    if (any) a.Add("--any-build");
                    if (!Step(2, SetupTool.Run(dir, OnLine, a.ToArray()), false)) return;
                }
                // 3 WXR's own files (the broker and the apps hold them: stop the broker, refuse while apps run)
                Ui(delegate { stepList.Current = 3; });
                string stop = StopWxr(dir);
                if (stop != null) { Fail(stop); return; }
                if (!Step(3, SetupTool.Run(dir, OnLine, "install-layer", Path.Combine(bundle, "layer")))) return;
                // 4 registry changes outside the folder, VxKex for the broker
                if (!Step(4, SetupTool.Run(dir, OnLine, "register"))) return;
                // 5 the optional components: added and removed to match the list (GDK downloads; UWP added later reads the ISO)
                List<string> c = new List<string>(new string[] { "components", compList, "--bundle", bundle });
                if (needIso && iso.Length > 0) { c.Add("--iso"); c.Add(iso); }
                if (any) c.Add("--any-build");
                if (!Step(5, SetupTool.Run(dir, OnLine, c.ToArray()))) return;
                // 6 the setup program into <root>\Setup + Programs and Features
                if (!Step(6, SetupTool.Run(dir, OnLine, "install-setup", bundle))) return;
                string done = "WXR does not come with any apps. The links below explain how to get them.";
                if (comps[CompMsa]) done += "\nTo use your Microsoft account in the Store, open WXR Sign-in (Start menu, WXR folder).";
                if (comps[CompGdk]) done += "\nGDK games are added with wxrpkg add; their GameInput is set up then.";
                Ui(delegate { Finished(flow == Flow.Repair ? "WXR is repaired" : "WXR is installed", done, true); });
            });
        }

        // ------------------------------------------------------------------ uninstall

        void BeginUninstall()
        {
            // the folder is fixed here, before anything else happens, and only this local is used from now on
            string dir = installedRoot;
            if (dir == null || !IsWxrInstall(dir))
            {
                MessageBox.Show(this, "There is no WXR installation at " + (dir ?? "(unknown)") + " to remove. Nothing was changed.", Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close(); return;
            }
            // the uninstaller runs from <root>\Setup, which purge deletes: continue from a copy in %TEMP%
            if (SameFolder(SetupTool.BundleDir, Path.Combine(dir, "Setup")))
            {
                try
                {
                    string tmp = Path.Combine(Path.GetTempPath(), "WXR-Uninstall-" + DateTime.Now.Ticks);
                    CopyBundle(SetupTool.BundleDir, tmp);
                    Process.Start(new ProcessStartInfo(Path.Combine(tmp, "WxrInstaller.exe"), "/uninstall /root \"" + dir + "\"") { UseShellExecute = true });
                }
                catch (Exception ex) { MessageBox.Show(this, "Cannot start the uninstaller: " + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); }
                Close();
                return;
            }
            if (MessageBox.Show(this, "Remove WXR from " + dir + "?\n\nThis also removes the Windows 10 files and every app installed in WXR, with its data.",
                                "Uninstall WXR", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes)
            {
                if (argUninstall) Close(); else ShowPage(pageMaintain);
                return;
            }
            flow = Flow.Uninstall;
            workRoot = dir;
            BeginSteps("Uninstalling", UninstallSteps);
            RunInBackground(delegate
            {
                string stop = StopWxr(dir);
                if (stop != null) { Fail(stop); return; }
                Ui(delegate { stepList.Current = 1; });
                // --expect: wxrsetup itself refuses to purge if the root it resolved is not exactly this folder
                if (!Step(1, SetupTool.Run(dir, OnLine, "--yes", "--expect", dir, "purge", "all"))) return;
                Ui(delegate { Finished("WXR is removed", "WXR and everything in " + dir + " were removed.", false); });
            });
        }

        static void CopyBundle(string from, string to)
        {
            Directory.CreateDirectory(to);
            foreach (string f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
            foreach (string sub in new string[] { "data", "licenses" })
            {
                string s = Path.Combine(from, sub);
                if (Directory.Exists(s)) CopyTree(s, Path.Combine(to, sub));
            }
        }

        static void CopyTree(string from, string to)
        {
            Directory.CreateDirectory(to);
            foreach (string f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
            foreach (string d in Directory.GetDirectories(from)) CopyTree(d, Path.Combine(to, Path.GetFileName(d)));
        }

        // ------------------------------------------------------------------ step plumbing

        void BeginSteps(string title, string[] steps)
        {
            installing = true; cancelRequested = false;
            titleLabel4.Text = title;
            stepList.Steps = steps; stepList.Failed = false; stepList.Current = 0;
            labelStepDetail.Text = "";
            progressInstall.Style = ProgressBarStyle.Marquee; progressInstall.Visible = true;
            ShowPage(pageProcess);
            buttonBack.Visible = buttonNext.Visible = false;
        }

        /// <summary>Marks step i running; on failure shows the problem and returns false. advance = move the list past step i when it worked.</summary>
        bool Step(int i, SetupResult r) { return Step(i, r, true); }
        bool Step(int i, SetupResult r, bool advance)
        {
            if (cancelRequested) return false;
            if (!r.Ok)
            {
                string why = r.Text("FAIL", "ERROR");
                if (why.Length == 0) why = r.Text("WARN");               // e.g. register: its problems are warnings plus a non-zero exit code
                Fail(why.Length > 0 ? why : "wxrsetup stopped with code " + r.ExitCode + ".", i);
                return false;
            }
            if (advance) Ui(delegate { stepList.Current = i + 1; labelStepDetail.Text = ""; });
            return true;
        }

        void Fail(string why) { Fail(why, -1); }
        void Fail(string why, int step)
        {
            string log = workRoot == null ? "" : Path.Combine(workRoot, @"Logs\setup.log");
            Ui(delegate
            {
                installing = false;
                if (step >= 0) stepList.Current = step;
                stepList.Failed = true;
                progressInstall.Visible = false;
                labelStepDetail.ForeColor = Color.FromArgb(190, 30, 30);
                labelStepDetail.Text = why + (File.Exists(log) ? "\nDetails: " + log : "");
                buttonCancel.Text = "Close";
            });
        }

        void Finished(string title, string text, bool showLinks)
        {
            installing = false;
            titleLabel6.Text = title; labelDone.Text = text;
            linkFaq.Visible = linkGetApps.Visible = showLinks;
            ShowPage(pageDone);
        }

        /// <summary>Progress and the current activity under the step list (called on the worker thread).</summary>
        void OnLine(SetupLine l)
        {
            if (l.Kind == "PROGRESS") Ui(delegate { labelStepDetail.ForeColor = SystemColors.GrayText; labelStepDetail.Text = char.ToUpper(l.Text[0]) + l.Text.Substring(1) + ": " + l.Key + "%"; });
            else if (l.Kind == "INFO" && !l.Text.StartsWith(" ")) Ui(delegate { labelStepDetail.ForeColor = SystemColors.GrayText; labelStepDetail.Text = l.Text; });
        }

        /// <summary>WXR's files are in use while the broker or an app runs. Stops an idle broker; returns an error text if apps are open.</summary>
        static string StopWxr(string dir)
        {
            string apps = Path.Combine(dir, "WindowsApps") + "\\", layer = Path.Combine(dir, "Layer") + "\\";
            List<string> open = new List<string>();
            foreach (Process p in ProcessesUnder(apps)) open.Add(p.ProcessName);
            if (open.Count > 0) return "Close these WXR apps first, then try again: " + string.Join(", ", open.ToArray());
            string broker = Path.Combine(dir, @"Layer\wxrsvc.exe");
            if (File.Exists(broker))
            {
                try
                {
                    using (Process s = Process.Start(new ProcessStartInfo(broker, "--wxr \"" + dir + "\" stop") { UseShellExecute = false, CreateNoWindow = true }))
                        s.WaitForExit(10000);
                }
                catch (System.ComponentModel.Win32Exception) { }
            }
            for (int t = 0; t < 20 && ProcessesUnder(layer).Count > 0; t++) Thread.Sleep(500);
            return ProcessesUnder(layer).Count > 0 ? "WXR's broker (" + broker + ") does not stop. End it in Task Manager and try again." : null;
        }

        static List<Process> ProcessesUnder(string dir)
        {
            List<Process> r = new List<Process>();
            foreach (Process p in Process.GetProcesses())
            {
                try { if (p.MainModule.FileName.StartsWith(dir, StringComparison.OrdinalIgnoreCase)) r.Add(p); }
                catch (System.ComponentModel.Win32Exception) { } catch (InvalidOperationException) { }
            }
            return r;
        }

        // ------------------------------------------------------------------ helpers

        void RunInBackground(ThreadStart work)
        {
            Thread t = new Thread(work); t.IsBackground = true; t.Start();
        }

        void Ui(MethodInvoker m)
        {
            try { if (!IsDisposed && IsHandleCreated) BeginInvoke(m); } catch (InvalidOperationException) { }   // the window is closing
        }

        static void OpenUrl(string url) { try { Process.Start(url); } catch (System.ComponentModel.Win32Exception) { } }

        /// <summary>
        /// The installation this setup program belongs to: the parent of &lt;root&gt;\Setup when it runs from there, else null.
        /// No guessing from the registry and no default folder: with several installations either could be wrong.
        /// </summary>
        static string FindInstalledRoot()
        {
            string me = Path.GetFullPath(Application.StartupPath).TrimEnd('\\');
            if (!Path.GetFileName(me).Equals("Setup", StringComparison.OrdinalIgnoreCase)) return null;
            string parent = Path.GetDirectoryName(me);
            return IsWxrInstall(parent) ? parent : null;
        }

        /// <summary>A WXR installation: environment.ini with the WXR marker, and its setup program in Setup\.</summary>
        static bool IsWxrInstall(string dir)
        {
            try
            {
                if (string.IsNullOrEmpty(dir) || !Path.IsPathRooted(dir)) return false;
                string ini = Path.Combine(dir, "environment.ini");
                if (!File.Exists(ini) || !File.Exists(Path.Combine(dir, @"Setup\WxrInstaller.exe"))) return false;
                foreach (string l in File.ReadAllLines(ini)) if (l.Trim() == "marker=WXR environment root") return true;
                return false;
            }
            catch (IOException) { return false; } catch (UnauthorizedAccessException) { return false; } catch (ArgumentException) { return false; }
        }
    }
}
