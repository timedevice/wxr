namespace WxrInstaller
{
    partial class Main
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.wizard = new WxrInstaller.Controls.WizardPages();
            this.pageStart = new System.Windows.Forms.TabPage();
            this.titleLabel1 = new System.Windows.Forms.Label();
            this.linkExpress = new WxrInstaller.Controls.CommandLink();
            this.linkCustom = new WxrInstaller.Controls.CommandLink();
            this.labelCheckNote = new System.Windows.Forms.Label();
            this.pagePaths = new System.Windows.Forms.TabPage();
            this.titleLabel2 = new System.Windows.Forms.Label();
            this.labelIso = new System.Windows.Forms.Label();
            this.textIso = new System.Windows.Forms.TextBox();
            this.buttonIsoBrowse = new System.Windows.Forms.Button();
            this.labelIsoStatus = new System.Windows.Forms.Label();
            this.labelFolder = new System.Windows.Forms.Label();
            this.textFolder = new System.Windows.Forms.TextBox();
            this.buttonFolderBrowse = new System.Windows.Forms.Button();
            this.labelFolderStatus = new System.Windows.Forms.Label();
            this.labelIsoWhy = new System.Windows.Forms.Label();
            this.linkGetIso = new System.Windows.Forms.LinkLabel();
            this.pageComponents = new System.Windows.Forms.TabPage();
            this.titleLabel3 = new System.Windows.Forms.Label();
            this.listComponents = new System.Windows.Forms.CheckedListBox();
            this.textComponentInfo = new System.Windows.Forms.TextBox();
            this.pageProcess = new System.Windows.Forms.TabPage();
            this.titleLabel4 = new System.Windows.Forms.Label();
            this.stepList = new WxrInstaller.Controls.StepList();
            this.labelStepDetail = new System.Windows.Forms.Label();
            this.progressInstall = new System.Windows.Forms.ProgressBar();
            this.pageBlocked = new System.Windows.Forms.TabPage();
            this.titleLabel5 = new System.Windows.Forms.Label();
            this.labelBlocked = new System.Windows.Forms.Label();
            this.pageDone = new System.Windows.Forms.TabPage();
            this.titleLabel6 = new System.Windows.Forms.Label();
            this.labelDone = new System.Windows.Forms.Label();
            this.linkFaq = new System.Windows.Forms.LinkLabel();
            this.linkGetApps = new System.Windows.Forms.LinkLabel();
            this.pageMaintain = new System.Windows.Forms.TabPage();
            this.titleLabel7 = new System.Windows.Forms.Label();
            this.labelMaintain = new System.Windows.Forms.Label();
            this.linkRepair = new WxrInstaller.Controls.CommandLink();
            this.linkUninstall = new WxrInstaller.Controls.CommandLink();
            this.buttonBar = new System.Windows.Forms.Panel();
            this.buttonBarLine = new System.Windows.Forms.Label();
            this.buttonBack = new System.Windows.Forms.Button();
            this.buttonNext = new System.Windows.Forms.Button();
            this.buttonCancel = new System.Windows.Forms.Button();
            this.wizard.SuspendLayout();
            this.pageStart.SuspendLayout();
            this.pagePaths.SuspendLayout();
            this.pageComponents.SuspendLayout();
            this.pageProcess.SuspendLayout();
            this.pageBlocked.SuspendLayout();
            this.pageDone.SuspendLayout();
            this.pageMaintain.SuspendLayout();
            this.buttonBar.SuspendLayout();
            this.SuspendLayout();
            //
            // wizard
            //
            this.wizard.Controls.Add(this.pageStart);
            this.wizard.Controls.Add(this.pagePaths);
            this.wizard.Controls.Add(this.pageComponents);
            this.wizard.Controls.Add(this.pageProcess);
            this.wizard.Controls.Add(this.pageBlocked);
            this.wizard.Controls.Add(this.pageDone);
            this.wizard.Controls.Add(this.pageMaintain);
            this.wizard.Dock = System.Windows.Forms.DockStyle.Fill;
            this.wizard.Location = new System.Drawing.Point(0, 0);
            this.wizard.Name = "wizard";
            this.wizard.SelectedIndex = 0;
            this.wizard.Size = new System.Drawing.Size(600, 420);
            this.wizard.TabIndex = 0;
            //
            // pageStart
            //
            this.pageStart.BackColor = System.Drawing.SystemColors.Window;
            this.pageStart.Controls.Add(this.titleLabel1);
            this.pageStart.Controls.Add(this.linkExpress);
            this.pageStart.Controls.Add(this.linkCustom);
            this.pageStart.Controls.Add(this.labelCheckNote);
            this.pageStart.Location = new System.Drawing.Point(4, 22);
            this.pageStart.Name = "pageStart";
            this.pageStart.Padding = new System.Windows.Forms.Padding(3);
            this.pageStart.Size = new System.Drawing.Size(592, 394);
            this.pageStart.TabIndex = 0;
            this.pageStart.Text = "Start";
            //
            // titleLabel1
            //
            this.titleLabel1.AutoSize = true;
            this.titleLabel1.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel1.Location = new System.Drawing.Point(40, 40);
            this.titleLabel1.Name = "titleLabel1";
            this.titleLabel1.Size = new System.Drawing.Size(142, 37);
            this.titleLabel1.TabIndex = 2;
            this.titleLabel1.Text = "WXR Setup";
            //
            // linkExpress
            //
            this.linkExpress.FlatStyle = System.Windows.Forms.FlatStyle.System;
            this.linkExpress.Location = new System.Drawing.Point(40, 120);
            this.linkExpress.Name = "linkExpress";
            this.linkExpress.Note = "Installs to C:\\WXR with the standard components.";
            this.linkExpress.Size = new System.Drawing.Size(500, 60);
            this.linkExpress.TabIndex = 0;
            this.linkExpress.Text = "Express install (recommended)";
            this.linkExpress.Click += new System.EventHandler(this.linkExpress_Click);
            //
            // linkCustom
            //
            this.linkCustom.FlatStyle = System.Windows.Forms.FlatStyle.System;
            this.linkCustom.Location = new System.Drawing.Point(40, 190);
            this.linkCustom.Name = "linkCustom";
            this.linkCustom.Note = "Choose the install location and the components.";
            this.linkCustom.Size = new System.Drawing.Size(500, 60);
            this.linkCustom.TabIndex = 1;
            this.linkCustom.Text = "Custom install";
            this.linkCustom.Click += new System.EventHandler(this.linkCustom_Click);
            //
            // labelCheckNote
            //
            this.labelCheckNote.ForeColor = System.Drawing.SystemColors.GrayText;
            this.labelCheckNote.Location = new System.Drawing.Point(46, 300);
            this.labelCheckNote.Name = "labelCheckNote";
            this.labelCheckNote.Size = new System.Drawing.Size(494, 60);
            this.labelCheckNote.TabIndex = 3;
            this.labelCheckNote.Text = "(system check result: warnings appear here)";
            //
            // pagePaths
            //
            this.pagePaths.BackColor = System.Drawing.SystemColors.Window;
            this.pagePaths.Controls.Add(this.titleLabel2);
            this.pagePaths.Controls.Add(this.labelIso);
            this.pagePaths.Controls.Add(this.textIso);
            this.pagePaths.Controls.Add(this.buttonIsoBrowse);
            this.pagePaths.Controls.Add(this.labelIsoStatus);
            this.pagePaths.Controls.Add(this.labelFolder);
            this.pagePaths.Controls.Add(this.textFolder);
            this.pagePaths.Controls.Add(this.buttonFolderBrowse);
            this.pagePaths.Controls.Add(this.labelFolderStatus);
            this.pagePaths.Controls.Add(this.labelIsoWhy);
            this.pagePaths.Controls.Add(this.linkGetIso);
            this.pagePaths.Location = new System.Drawing.Point(4, 22);
            this.pagePaths.Name = "pagePaths";
            this.pagePaths.Padding = new System.Windows.Forms.Padding(3);
            this.pagePaths.Size = new System.Drawing.Size(592, 394);
            this.pagePaths.TabIndex = 1;
            this.pagePaths.Text = "Selection";
            //
            // titleLabel2
            //
            this.titleLabel2.AutoSize = true;
            this.titleLabel2.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel2.Location = new System.Drawing.Point(40, 40);
            this.titleLabel2.Name = "titleLabel2";
            this.titleLabel2.Size = new System.Drawing.Size(165, 37);
            this.titleLabel2.TabIndex = 3;
            this.titleLabel2.Text = "Select an ISO";
            //
            // labelIso
            //
            this.labelIso.AutoSize = true;
            this.labelIso.Location = new System.Drawing.Point(44, 105);
            this.labelIso.Name = "labelIso";
            this.labelIso.Size = new System.Drawing.Size(104, 13);
            this.labelIso.TabIndex = 4;
            this.labelIso.Text = "Windows 10 ISO file:";
            //
            // textIso
            //
            this.textIso.Location = new System.Drawing.Point(47, 123);
            this.textIso.Name = "textIso";
            this.textIso.Size = new System.Drawing.Size(412, 20);
            this.textIso.TabIndex = 5;
            this.textIso.TextChanged += new System.EventHandler(this.textIso_TextChanged);
            //
            // buttonIsoBrowse
            //
            this.buttonIsoBrowse.Location = new System.Drawing.Point(465, 121);
            this.buttonIsoBrowse.Name = "buttonIsoBrowse";
            this.buttonIsoBrowse.Size = new System.Drawing.Size(75, 23);
            this.buttonIsoBrowse.TabIndex = 6;
            this.buttonIsoBrowse.Text = "Browse...";
            this.buttonIsoBrowse.UseVisualStyleBackColor = true;
            this.buttonIsoBrowse.Click += new System.EventHandler(this.buttonIsoBrowse_Click);
            //
            // labelIsoStatus
            //
            this.labelIsoStatus.Location = new System.Drawing.Point(44, 148);
            this.labelIsoStatus.Name = "labelIsoStatus";
            this.labelIsoStatus.Size = new System.Drawing.Size(496, 32);
            this.labelIsoStatus.TabIndex = 7;
            //
            // labelFolder
            //
            this.labelFolder.AutoSize = true;
            this.labelFolder.Location = new System.Drawing.Point(44, 200);
            this.labelFolder.Name = "labelFolder";
            this.labelFolder.Size = new System.Drawing.Size(54, 13);
            this.labelFolder.TabIndex = 8;
            this.labelFolder.Text = "Install to:";
            //
            // textFolder
            //
            this.textFolder.Location = new System.Drawing.Point(47, 218);
            this.textFolder.Name = "textFolder";
            this.textFolder.Size = new System.Drawing.Size(412, 20);
            this.textFolder.TabIndex = 9;
            this.textFolder.Text = "C:\\WXR";
            this.textFolder.TextChanged += new System.EventHandler(this.textFolder_TextChanged);
            //
            // buttonFolderBrowse
            //
            this.buttonFolderBrowse.Location = new System.Drawing.Point(465, 216);
            this.buttonFolderBrowse.Name = "buttonFolderBrowse";
            this.buttonFolderBrowse.Size = new System.Drawing.Size(75, 23);
            this.buttonFolderBrowse.TabIndex = 10;
            this.buttonFolderBrowse.Text = "Browse...";
            this.buttonFolderBrowse.UseVisualStyleBackColor = true;
            this.buttonFolderBrowse.Click += new System.EventHandler(this.buttonFolderBrowse_Click);
            //
            // labelFolderStatus
            //
            this.labelFolderStatus.Location = new System.Drawing.Point(44, 243);
            this.labelFolderStatus.Name = "labelFolderStatus";
            this.labelFolderStatus.Size = new System.Drawing.Size(496, 32);
            this.labelFolderStatus.TabIndex = 11;
            //
            // labelIsoWhy
            //
            this.labelIsoWhy.ForeColor = System.Drawing.SystemColors.GrayText;
            this.labelIsoWhy.Location = new System.Drawing.Point(44, 288);
            this.labelIsoWhy.Name = "labelIsoWhy";
            this.labelIsoWhy.Size = new System.Drawing.Size(496, 72);
            this.labelIsoWhy.TabIndex = 12;
            this.labelIsoWhy.Text = "Why an ISO? WXR runs apps on Microsoft's own Windows 10 system files (XAML, media, graphics and more). They cannot be shipped with WXR, so setup copies the ones it needs (about 450 MB) from your Windows 10 ISO. The ISO is only read: keep it or delete it afterwards. No product key or activation is needed. Use Windows 10 version 22H2, 64-bit.";
            //
            // linkGetIso
            //
            this.linkGetIso.AutoSize = true;
            this.linkGetIso.Location = new System.Drawing.Point(44, 364);
            this.linkGetIso.Name = "linkGetIso";
            this.linkGetIso.Size = new System.Drawing.Size(190, 13);
            this.linkGetIso.TabIndex = 13;
            this.linkGetIso.TabStop = true;
            this.linkGetIso.Text = "Download Windows 10 from Microsoft";
            //
            // pageComponents
            //
            this.pageComponents.BackColor = System.Drawing.SystemColors.Window;
            this.pageComponents.Controls.Add(this.textComponentInfo);
            this.pageComponents.Controls.Add(this.listComponents);
            this.pageComponents.Controls.Add(this.titleLabel3);
            this.pageComponents.Location = new System.Drawing.Point(4, 22);
            this.pageComponents.Name = "pageComponents";
            this.pageComponents.Padding = new System.Windows.Forms.Padding(3);
            this.pageComponents.Size = new System.Drawing.Size(592, 394);
            this.pageComponents.TabIndex = 2;
            this.pageComponents.Text = "Components";
            //
            // titleLabel3
            //
            this.titleLabel3.AutoSize = true;
            this.titleLabel3.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel3.Location = new System.Drawing.Point(40, 40);
            this.titleLabel3.Name = "titleLabel3";
            this.titleLabel3.Size = new System.Drawing.Size(230, 37);
            this.titleLabel3.TabIndex = 3;
            this.titleLabel3.Text = "Select components";
            //
            // listComponents
            //
            this.listComponents.CheckOnClick = true;
            this.listComponents.FormattingEnabled = true;
            this.listComponents.Items.AddRange(new object[] {
            "UWP apps",
            "GDK games",
            "Microsoft account sign-in"});
            this.listComponents.Location = new System.Drawing.Point(47, 120);
            this.listComponents.Name = "listComponents";
            this.listComponents.Size = new System.Drawing.Size(200, 169);
            this.listComponents.TabIndex = 4;
            this.listComponents.ItemCheck += new System.Windows.Forms.ItemCheckEventHandler(this.listComponents_ItemCheck);
            this.listComponents.SelectedIndexChanged += new System.EventHandler(this.listComponents_SelectedIndexChanged);
            //
            // textComponentInfo
            //
            this.textComponentInfo.BackColor = System.Drawing.SystemColors.Window;
            this.textComponentInfo.Location = new System.Drawing.Point(266, 120);
            this.textComponentInfo.Multiline = true;
            this.textComponentInfo.Name = "textComponentInfo";
            this.textComponentInfo.ReadOnly = true;
            this.textComponentInfo.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this.textComponentInfo.Size = new System.Drawing.Size(274, 169);
            this.textComponentInfo.TabIndex = 5;
            this.textComponentInfo.TabStop = false;
            //
            // pageProcess
            //
            this.pageProcess.BackColor = System.Drawing.SystemColors.Window;
            this.pageProcess.Controls.Add(this.titleLabel4);
            this.pageProcess.Controls.Add(this.stepList);
            this.pageProcess.Controls.Add(this.labelStepDetail);
            this.pageProcess.Controls.Add(this.progressInstall);
            this.pageProcess.Location = new System.Drawing.Point(4, 22);
            this.pageProcess.Name = "pageProcess";
            this.pageProcess.Padding = new System.Windows.Forms.Padding(3);
            this.pageProcess.Size = new System.Drawing.Size(592, 394);
            this.pageProcess.TabIndex = 3;
            this.pageProcess.Text = "Process";
            //
            // titleLabel4
            //
            this.titleLabel4.AutoSize = true;
            this.titleLabel4.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel4.Location = new System.Drawing.Point(40, 40);
            this.titleLabel4.Name = "titleLabel4";
            this.titleLabel4.Size = new System.Drawing.Size(115, 37);
            this.titleLabel4.TabIndex = 3;
            this.titleLabel4.Text = "Installing";
            //
            // stepList
            //
            this.stepList.Location = new System.Drawing.Point(43, 100);
            this.stepList.Name = "stepList";
            this.stepList.Size = new System.Drawing.Size(497, 200);
            this.stepList.Steps = new string[] {
        "Checking this computer",
        "Reading the Windows 10 ISO",
        "Copying Windows 10 files",
        "Installing WXR",
        "Registering",
        "Finishing up"};
            this.stepList.TabIndex = 4;
            //
            // labelStepDetail
            //
            this.labelStepDetail.ForeColor = System.Drawing.SystemColors.GrayText;
            this.labelStepDetail.Location = new System.Drawing.Point(44, 310);
            this.labelStepDetail.Name = "labelStepDetail";
            this.labelStepDetail.Size = new System.Drawing.Size(496, 40);
            this.labelStepDetail.TabIndex = 5;
            //
            // progressInstall
            //
            this.progressInstall.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.progressInstall.Location = new System.Drawing.Point(0, 371);
            this.progressInstall.MarqueeAnimationSpeed = 30;
            this.progressInstall.Name = "progressInstall";
            this.progressInstall.Size = new System.Drawing.Size(592, 23);
            this.progressInstall.Style = System.Windows.Forms.ProgressBarStyle.Marquee;
            this.progressInstall.TabIndex = 6;
            //
            // pageBlocked
            //
            this.pageBlocked.BackColor = System.Drawing.SystemColors.Window;
            this.pageBlocked.Controls.Add(this.titleLabel5);
            this.pageBlocked.Controls.Add(this.labelBlocked);
            this.pageBlocked.Location = new System.Drawing.Point(4, 22);
            this.pageBlocked.Name = "pageBlocked";
            this.pageBlocked.Padding = new System.Windows.Forms.Padding(3);
            this.pageBlocked.Size = new System.Drawing.Size(592, 394);
            this.pageBlocked.TabIndex = 4;
            this.pageBlocked.Text = "Blocked";
            //
            // titleLabel5
            //
            this.titleLabel5.AutoSize = true;
            this.titleLabel5.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel5.Location = new System.Drawing.Point(40, 40);
            this.titleLabel5.Name = "titleLabel5";
            this.titleLabel5.Size = new System.Drawing.Size(334, 37);
            this.titleLabel5.TabIndex = 0;
            this.titleLabel5.Text = "WXR can\'t be installed yet";
            //
            // labelBlocked
            //
            this.labelBlocked.Location = new System.Drawing.Point(44, 100);
            this.labelBlocked.Name = "labelBlocked";
            this.labelBlocked.Size = new System.Drawing.Size(496, 250);
            this.labelBlocked.TabIndex = 1;
            this.labelBlocked.Text = "(what is missing and how to fix it)";
            //
            // pageDone
            //
            this.pageDone.BackColor = System.Drawing.SystemColors.Window;
            this.pageDone.Controls.Add(this.titleLabel6);
            this.pageDone.Controls.Add(this.labelDone);
            this.pageDone.Controls.Add(this.linkFaq);
            this.pageDone.Controls.Add(this.linkGetApps);
            this.pageDone.Location = new System.Drawing.Point(4, 22);
            this.pageDone.Name = "pageDone";
            this.pageDone.Padding = new System.Windows.Forms.Padding(3);
            this.pageDone.Size = new System.Drawing.Size(592, 394);
            this.pageDone.TabIndex = 5;
            this.pageDone.Text = "Done";
            //
            // titleLabel6
            //
            this.titleLabel6.AutoSize = true;
            this.titleLabel6.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel6.Location = new System.Drawing.Point(40, 40);
            this.titleLabel6.Name = "titleLabel6";
            this.titleLabel6.Size = new System.Drawing.Size(212, 37);
            this.titleLabel6.TabIndex = 0;
            this.titleLabel6.Text = "WXR is installed";
            //
            // labelDone
            //
            this.labelDone.Location = new System.Drawing.Point(44, 100);
            this.labelDone.Name = "labelDone";
            this.labelDone.Size = new System.Drawing.Size(496, 60);
            this.labelDone.TabIndex = 1;
            this.labelDone.Text = "WXR does not come with any apps. The links below explain how to get them.";
            //
            // linkFaq
            //
            this.linkFaq.AutoSize = true;
            this.linkFaq.Location = new System.Drawing.Point(44, 170);
            this.linkFaq.Name = "linkFaq";
            this.linkFaq.Size = new System.Drawing.Size(150, 13);
            this.linkFaq.TabIndex = 2;
            this.linkFaq.TabStop = true;
            this.linkFaq.Text = "Frequently asked questions";
            //
            // linkGetApps
            //
            this.linkGetApps.AutoSize = true;
            this.linkGetApps.Location = new System.Drawing.Point(44, 195);
            this.linkGetApps.Name = "linkGetApps";
            this.linkGetApps.Size = new System.Drawing.Size(120, 13);
            this.linkGetApps.TabIndex = 3;
            this.linkGetApps.TabStop = true;
            this.linkGetApps.Text = "Where to get apps";
            //
            // pageMaintain
            //
            this.pageMaintain.BackColor = System.Drawing.SystemColors.Window;
            this.pageMaintain.Controls.Add(this.titleLabel7);
            this.pageMaintain.Controls.Add(this.labelMaintain);
            this.pageMaintain.Controls.Add(this.linkRepair);
            this.pageMaintain.Controls.Add(this.linkUninstall);
            this.pageMaintain.Location = new System.Drawing.Point(4, 22);
            this.pageMaintain.Name = "pageMaintain";
            this.pageMaintain.Padding = new System.Windows.Forms.Padding(3);
            this.pageMaintain.Size = new System.Drawing.Size(592, 394);
            this.pageMaintain.TabIndex = 6;
            this.pageMaintain.Text = "Maintain";
            //
            // titleLabel7
            //
            this.titleLabel7.AutoSize = true;
            this.titleLabel7.Font = new System.Drawing.Font("Segoe UI Light", 20.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.titleLabel7.Location = new System.Drawing.Point(40, 40);
            this.titleLabel7.Name = "titleLabel7";
            this.titleLabel7.Size = new System.Drawing.Size(169, 37);
            this.titleLabel7.TabIndex = 0;
            this.titleLabel7.Text = "Change WXR";
            //
            // labelMaintain
            //
            this.labelMaintain.Location = new System.Drawing.Point(44, 90);
            this.labelMaintain.Name = "labelMaintain";
            this.labelMaintain.Size = new System.Drawing.Size(496, 20);
            this.labelMaintain.TabIndex = 1;
            this.labelMaintain.Text = "(install folder)";
            //
            // linkRepair
            //
            this.linkRepair.FlatStyle = System.Windows.Forms.FlatStyle.System;
            this.linkRepair.Location = new System.Drawing.Point(40, 120);
            this.linkRepair.Name = "linkRepair";
            this.linkRepair.Note = "Puts WXR's own files back in place and redoes its Windows settings. Installed apps are kept.";
            this.linkRepair.Size = new System.Drawing.Size(500, 60);
            this.linkRepair.TabIndex = 2;
            this.linkRepair.Text = "Repair or update";
            this.linkRepair.Click += new System.EventHandler(this.linkRepair_Click);
            //
            // linkUninstall
            //
            this.linkUninstall.FlatStyle = System.Windows.Forms.FlatStyle.System;
            this.linkUninstall.Location = new System.Drawing.Point(40, 190);
            this.linkUninstall.Name = "linkUninstall";
            this.linkUninstall.Note = "Removes WXR, the Windows 10 files and all apps installed in it.";
            this.linkUninstall.Size = new System.Drawing.Size(500, 60);
            this.linkUninstall.TabIndex = 3;
            this.linkUninstall.Text = "Uninstall";
            this.linkUninstall.Click += new System.EventHandler(this.linkUninstall_Click);
            //
            // buttonBar
            //
            this.buttonBar.BackColor = System.Drawing.SystemColors.Control;
            this.buttonBar.Controls.Add(this.buttonBarLine);
            this.buttonBar.Controls.Add(this.buttonBack);
            this.buttonBar.Controls.Add(this.buttonNext);
            this.buttonBar.Controls.Add(this.buttonCancel);
            this.buttonBar.Dock = System.Windows.Forms.DockStyle.Bottom;
            this.buttonBar.Location = new System.Drawing.Point(0, 420);
            this.buttonBar.Name = "buttonBar";
            this.buttonBar.Size = new System.Drawing.Size(600, 41);
            this.buttonBar.TabIndex = 1;
            //
            // buttonBarLine
            //
            this.buttonBarLine.BackColor = System.Drawing.SystemColors.ControlLight;
            this.buttonBarLine.Dock = System.Windows.Forms.DockStyle.Top;
            this.buttonBarLine.Location = new System.Drawing.Point(0, 0);
            this.buttonBarLine.Name = "buttonBarLine";
            this.buttonBarLine.Size = new System.Drawing.Size(600, 1);
            this.buttonBarLine.TabIndex = 3;
            //
            // buttonBack
            //
            this.buttonBack.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.buttonBack.Location = new System.Drawing.Point(351, 9);
            this.buttonBack.Name = "buttonBack";
            this.buttonBack.Size = new System.Drawing.Size(75, 23);
            this.buttonBack.TabIndex = 0;
            this.buttonBack.Text = "< Back";
            this.buttonBack.UseVisualStyleBackColor = true;
            this.buttonBack.Click += new System.EventHandler(this.buttonBack_Click);
            //
            // buttonNext
            //
            this.buttonNext.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.buttonNext.Location = new System.Drawing.Point(432, 9);
            this.buttonNext.Name = "buttonNext";
            this.buttonNext.Size = new System.Drawing.Size(75, 23);
            this.buttonNext.TabIndex = 1;
            this.buttonNext.Text = "Next >";
            this.buttonNext.UseVisualStyleBackColor = true;
            this.buttonNext.Click += new System.EventHandler(this.buttonNext_Click);
            //
            // buttonCancel
            //
            this.buttonCancel.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.buttonCancel.Location = new System.Drawing.Point(513, 9);
            this.buttonCancel.Name = "buttonCancel";
            this.buttonCancel.Size = new System.Drawing.Size(75, 23);
            this.buttonCancel.TabIndex = 2;
            this.buttonCancel.Text = "Cancel";
            this.buttonCancel.UseVisualStyleBackColor = true;
            this.buttonCancel.Click += new System.EventHandler(this.buttonCancel_Click);
            //
            // Main
            //
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(600, 461);
            this.Controls.Add(this.wizard);
            this.Controls.Add(this.buttonBar);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.Name = "Main";
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "WXR Setup";
            this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.Main_FormClosing);
            this.wizard.ResumeLayout(false);
            this.pageStart.ResumeLayout(false);
            this.pageStart.PerformLayout();
            this.pagePaths.ResumeLayout(false);
            this.pagePaths.PerformLayout();
            this.pageComponents.ResumeLayout(false);
            this.pageComponents.PerformLayout();
            this.pageProcess.ResumeLayout(false);
            this.pageProcess.PerformLayout();
            this.pageBlocked.ResumeLayout(false);
            this.pageBlocked.PerformLayout();
            this.pageDone.ResumeLayout(false);
            this.pageDone.PerformLayout();
            this.pageMaintain.ResumeLayout(false);
            this.pageMaintain.PerformLayout();
            this.buttonBar.ResumeLayout(false);
            this.ResumeLayout(false);

        }

        #endregion

        private WxrInstaller.Controls.WizardPages wizard;
        private System.Windows.Forms.TabPage pageStart;
        private WxrInstaller.Controls.CommandLink linkExpress;
        private WxrInstaller.Controls.CommandLink linkCustom;
        private System.Windows.Forms.Label titleLabel1;
        private System.Windows.Forms.Label labelCheckNote;
        private System.Windows.Forms.TabPage pagePaths;
        private System.Windows.Forms.Label titleLabel2;
        private System.Windows.Forms.Label labelIso;
        private System.Windows.Forms.TextBox textIso;
        private System.Windows.Forms.Button buttonIsoBrowse;
        private System.Windows.Forms.Label labelIsoStatus;
        private System.Windows.Forms.Label labelFolder;
        private System.Windows.Forms.TextBox textFolder;
        private System.Windows.Forms.Button buttonFolderBrowse;
        private System.Windows.Forms.Label labelFolderStatus;
        private System.Windows.Forms.Label labelIsoWhy;
        private System.Windows.Forms.LinkLabel linkGetIso;
        private System.Windows.Forms.TabPage pageComponents;
        private System.Windows.Forms.TextBox textComponentInfo;
        private System.Windows.Forms.CheckedListBox listComponents;
        private System.Windows.Forms.Label titleLabel3;
        private System.Windows.Forms.TabPage pageProcess;
        private System.Windows.Forms.Label titleLabel4;
        private WxrInstaller.Controls.StepList stepList;
        private System.Windows.Forms.Label labelStepDetail;
        private System.Windows.Forms.ProgressBar progressInstall;
        private System.Windows.Forms.TabPage pageBlocked;
        private System.Windows.Forms.Label titleLabel5;
        private System.Windows.Forms.Label labelBlocked;
        private System.Windows.Forms.TabPage pageDone;
        private System.Windows.Forms.Label titleLabel6;
        private System.Windows.Forms.Label labelDone;
        private System.Windows.Forms.LinkLabel linkFaq;
        private System.Windows.Forms.LinkLabel linkGetApps;
        private System.Windows.Forms.TabPage pageMaintain;
        private System.Windows.Forms.Label titleLabel7;
        private System.Windows.Forms.Label labelMaintain;
        private WxrInstaller.Controls.CommandLink linkRepair;
        private WxrInstaller.Controls.CommandLink linkUninstall;
        private System.Windows.Forms.Panel buttonBar;
        private System.Windows.Forms.Label buttonBarLine;
        private System.Windows.Forms.Button buttonBack;
        private System.Windows.Forms.Button buttonNext;
        private System.Windows.Forms.Button buttonCancel;
    }
}
