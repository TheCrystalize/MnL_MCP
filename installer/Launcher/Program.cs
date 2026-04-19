using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing;

namespace Launcher
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            ApplicationConfiguration.Initialize();
            Application.Run(new MainForm());
        }
    }

    public class MainForm : Form
    {
        private TextBox txtServerPath;
        private Button btnBrowse;
        private TextBox txtArgs;
        private Button btnStart;
        private Button btnStop;
        private Button btnOpenFolder;
        private Label lblStatus;
        private TextBox txtLogs;
        private Process serverProcess;
        private readonly string appDir;

        public MainForm()
        {
            appDir = AppContext.BaseDirectory;
            InitializeComponent();

            // Set window icon from the embedded application icon or fallback to bundled MnLLogo.ico
            try
            {
                Icon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                var iconPath = Path.Combine(appDir, "MnLLogo.ico");
                if (File.Exists(iconPath))
                {
                    try { Icon = new System.Drawing.Icon(iconPath); } catch { /* ignore */ }
                }
            }
        }

        private void InitializeComponent()
        {
            Text = "MCP Server Launcher";
            Width = 900;
            Height = 640;
            StartPosition = FormStartPosition.CenterScreen;

            var topPanel = new TableLayoutPanel
            {
                Dock = DockStyle.Top,
                Height = 120,
                ColumnCount = 3,
                RowCount = 3,
                Padding = new Padding(8),
                AutoSize = true
            };
            topPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 70F));
            topPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 20F));
            topPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 10F));
            topPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            topPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            topPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            var lblPath = new Label { Text = "Server exe:", Anchor = AnchorStyles.Left, AutoSize = true };
            txtServerPath = new TextBox { Text = Path.Combine(appDir, "mcp_stdio_server.exe"), Dock = DockStyle.Fill };
            btnBrowse = new Button { Text = "Browse..." };
            btnBrowse.Click += BtnBrowse_Click;

            topPanel.Controls.Add(lblPath, 0, 0);
            topPanel.Controls.Add(txtServerPath, 1, 0);
            topPanel.Controls.Add(btnBrowse, 2, 0);

            var lblArgs = new Label { Text = "Arguments:", Anchor = AnchorStyles.Left, AutoSize = true };
            txtArgs = new TextBox { Text = "", Dock = DockStyle.Fill };
            topPanel.Controls.Add(lblArgs, 0, 1);
            topPanel.Controls.Add(txtArgs, 1, 1);

            var buttonsPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.LeftToRight, AutoSize = true };
            btnStart = new Button { Text = "Start", AutoSize = true };
            btnStart.Click += async (s, e) => await StartServerAsync();
            btnStop = new Button { Text = "Stop", AutoSize = true, Enabled = false };
            btnStop.Click += (s, e) => StopServer();
            btnOpenFolder = new Button { Text = "Open Folder", AutoSize = true };
            btnOpenFolder.Click += BtnOpenFolder_Click;
            buttonsPanel.Controls.Add(btnStart);
            buttonsPanel.Controls.Add(btnStop);
            buttonsPanel.Controls.Add(btnOpenFolder);

            Button btnOpenConsole = new Button { Text = "Open Console", AutoSize = true };
            btnOpenConsole.Click += BtnOpenConsole_Click;
            buttonsPanel.Controls.Add(btnOpenConsole);

            lblStatus = new Label { Text = "Stopped", Anchor = AnchorStyles.Left, AutoSize = true, Padding = new Padding(10, 6, 0, 0) };

            topPanel.Controls.Add(buttonsPanel, 0, 2);
            topPanel.Controls.Add(lblStatus, 1, 2);

            Controls.Add(topPanel);

            txtLogs = new TextBox
            {
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Both,
                Dock = DockStyle.Fill,
                WordWrap = false,
                Font = new System.Drawing.Font("Consolas", 10)
            };
            Controls.Add(txtLogs);

            FormClosing += MainForm_FormClosing;
        }

        private void BtnBrowse_Click(object? sender, EventArgs e)
        {
            using var ofd = new OpenFileDialog();
            ofd.Filter = "Executable (*.exe)|*.exe|All files|*.*";
            ofd.InitialDirectory = appDir;
            if (ofd.ShowDialog() == DialogResult.OK)
            {
                txtServerPath.Text = ofd.FileName;
            }
        }

        private void BtnOpenFolder_Click(object? sender, EventArgs e)
        {
            try
            {
                var folder = Path.GetDirectoryName(txtServerPath.Text) ?? appDir;
                Process.Start(new ProcessStartInfo { FileName = "explorer.exe", Arguments = $"\"{folder}\"", UseShellExecute = true });
            }
            catch (Exception ex)
            {
                AppendLog($"Error opening folder: {ex.Message}");
            }
        }

        private void BtnOpenConsole_Click(object? sender, EventArgs e)
        {
            try
            {
                var exe = txtServerPath.Text;
                var folder = Path.GetDirectoryName(exe) ?? appDir;
                // Launch cmd.exe with correct environment and path to server
                var startInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/k \"echo MnL MCP Server Console && echo. && PATH={folder};%PATH% && PYTHONPATH={folder} && echo Ready. Run '{Path.GetFileName(exe)}' to start server. && echo.\"",
                    WorkingDirectory = folder,
                    UseShellExecute = true,
                    CreateNoWindow = false
                };
                Process.Start(startInfo);
                AppendLog("Opened command console for server");
            }
            catch (Exception ex)
            {
                AppendLog($"Error opening console: {ex.Message}");
            }
        }

        private Task StartServerAsync()
        {
            return Task.Run(() =>
            {
                var exe = txtServerPath.Text;
                if (string.IsNullOrWhiteSpace(exe) || !File.Exists(exe))
                {
                    AppendLog($"Server executable not found: {exe}");
                    return;
                }
                if (serverProcess != null && !serverProcess.HasExited)
                {
                    AppendLog("Server is already running.");
                    return;
                }
                try
                {
                    var psi = new ProcessStartInfo
                    {
                        FileName = exe,
                        Arguments = txtArgs.Text ?? "",
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true,
                        WorkingDirectory = Path.GetDirectoryName(exe) ?? appDir,
                    };

                    // Prepend exe folder to PATH so bundled DLLs (e.g., Z3) are found
                    var exeFolder = Path.GetDirectoryName(exe) ?? appDir;
                    var currentPath = Environment.GetEnvironmentVariable("PATH") ?? "";
                    psi.EnvironmentVariables["PATH"] = exeFolder + ";" + currentPath;
                    // Ensure Python can import bundled .pyd
                    psi.EnvironmentVariables["PYTHONPATH"] = exeFolder;

                    serverProcess = new Process { StartInfo = psi, EnableRaisingEvents = true };
                    serverProcess.OutputDataReceived += (s, e) => { if (e.Data != null) AppendLog(e.Data); };
                    serverProcess.ErrorDataReceived += (s, e) => {
                        // Do not prefix stderr lines that are part of the welcome banner (they use box drawing characters)
                        if (e.Data != null) {
                            if (e.Data.StartsWith("╔") || e.Data.StartsWith("║") || e.Data.StartsWith("╠") || e.Data.StartsWith("╚") || string.IsNullOrWhiteSpace(e.Data)) {
                                AppendLog(e.Data);
                            } else {
                                AppendLog("[ERR] " + e.Data);
                            }
                        }
                    };
                    serverProcess.Exited += (s, e) => { AppendLog($"Server exited with code {serverProcess.ExitCode}"); UpdateStatus(false); };

                    serverProcess.Start();
                    serverProcess.BeginOutputReadLine();
                    serverProcess.BeginErrorReadLine();
                    AppendLog($"Started server: {exe} (PID {serverProcess.Id})");
                    UpdateStatus(true);

                    // Auto-open console window when server starts
                    try
                    {
                        // Allocate console window for this process if not already attached
                        if (!ConsoleAllocator.IsConsoleAllocated())
                        {
                            ConsoleAllocator.AllocConsole();
                            Console.Title = "MnL MCP Server Log";
                            AppendLog("Opened console log window");
                        }
                    }
                    catch { /* ignore - console allocation is optional */ }
                }
                catch (Exception ex)
                {
                    AppendLog("Failed to start server: " + ex.Message);
                }
            });
        }

        private void StopServer()
        {
            try
            {
                if (serverProcess == null || serverProcess.HasExited)
                {
                    AppendLog("Server is not running.");
                    UpdateStatus(false);
                    return;
                }
                AppendLog("Stopping server...");
                try
                {
                    serverProcess.Kill(true);
                }
                catch
                {
                    serverProcess.Kill();
                }
                serverProcess.WaitForExit(5000);
                AppendLog($"Server stopped (exit code {serverProcess.ExitCode})");
                UpdateStatus(false);
            }
            catch (Exception ex)
            {
                AppendLog("Error stopping server: " + ex.Message);
            }
            finally
            {
                serverProcess = null;
            }
        }

        private void UpdateStatus(bool running)
        {
            if (InvokeRequired)
            {
                Invoke((Action)(() => UpdateStatus(running)));
                return;
            }
            lblStatus.Text = running ? $"Running (PID {serverProcess?.Id})" : "Stopped";
            btnStart.Enabled = !running;
            btnStop.Enabled = running;
        }

        private void AppendLog(string text)
        {
            if (InvokeRequired)
            {
                BeginInvoke((Action)(() => AppendLog(text)));
                return;
            }
            txtLogs.AppendText($"[{DateTime.Now:HH:mm:ss}] {text}{Environment.NewLine}");
            const int maxLines = 2000;
            var lines = txtLogs.Lines;
            if (lines.Length > maxLines)
            {
                var trimmed = lines[(lines.Length - maxLines)..];
                txtLogs.Lines = trimmed;
            }
            txtLogs.SelectionStart = txtLogs.Text.Length;
            txtLogs.ScrollToCaret();
        }

        private void MainForm_FormClosing(object? sender, FormClosingEventArgs e)
        {
            if (serverProcess != null && !serverProcess.HasExited)
            {
                var res = MessageBox.Show("Server is still running. Stop it and exit?", "Exit", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                if (res == DialogResult.Yes)
                {
                    StopServer();
                }
                else
                {
                    e.Cancel = true;
                }
            }
        }
    }

    internal static class ConsoleAllocator
    {
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        internal static extern bool AllocConsole();
        
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        internal static extern IntPtr GetConsoleWindow();

        internal static bool IsConsoleAllocated()
        {
            return GetConsoleWindow() != IntPtr.Zero;
        }
    }
}