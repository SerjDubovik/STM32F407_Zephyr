using System.Drawing.Drawing2D;

namespace SdCardBrowser;

internal sealed partial class BrowserForm
{
    private const int TouchWidth = 240;
    private const int TouchHeight = 320;
    private readonly TabControl _workspaceTabs = new() { Dock = DockStyle.Fill };
    private readonly TabPage _filesTab = new("Файлы") { UseVisualStyleBackColor = true };
    private readonly TabPage _touchTab = new("Тачскрин") { UseVisualStyleBackColor = true };
    private readonly Label _touchCoordinates = new()
    {
        Dock = DockStyle.Fill,
        TextAlign = ContentAlignment.MiddleCenter,
        Text = "X: —    Y: —"
    };
    private readonly TouchDisplay _touchDisplay = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer _touchTimer = new() { Interval = 80 };
    private bool _touchAvailable = true;
    private bool _mountOnFilesTab;
    private bool TouchTabSelected => _workspaceTabs.SelectedTab == _touchTab;
    private bool FilesTabSelected => _workspaceTabs.SelectedTab == _filesTab;

    private void InitializeTouchTab()
    {
        layout.Controls.Remove(panes);
        _filesTab.Controls.Add(panes);
        layout.Controls.Add(_workspaceTabs, 0, 1);
        _workspaceTabs.TabPages.Add(_filesTab);
        _workspaceTabs.TabPages.Add(_touchTab);

        var touchLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            Padding = new Padding(8)
        };
        touchLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        touchLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 32F));
        touchLayout.Controls.Add(_touchDisplay, 0, 0);
        touchLayout.Controls.Add(_touchCoordinates, 0, 1);
        _touchTab.Controls.Add(touchLayout);

        _workspaceTabs.Selecting += (_, e) => e.Cancel = _busy && _pollFinished is null;
        _workspaceTabs.SelectedIndexChanged += async (_, _) => await UpdateWorkspaceAsync();
        _touchTimer.Tick += async (_, _) => await PollTouchAsync();
        Disposed += (_, _) => _touchTimer.Dispose();
    }

    private async Task UpdateWorkspaceAsync()
    {
        bool files = FilesTabSelected;
        foreach (Control control in new Control[] { refreshButton, createButton, mkdirButton, renameButton,
                     moveButton, deleteButton, uploadButton, downloadButton, openButton, cancelButton })
        {
            control.Visible = files;
        }
        layout.RowStyles[0].Height = files ? 126F * DeviceDpi / 120F : baudComboBox.Bottom + baudComboBox.Top;
        freeSpaceLabel.Visible = files;
        _touchTimer.Enabled = TouchTabSelected;
        if (!TouchTabSelected) ShowTouchSample(null);
        if (!TasksTabSelected) _previousTasks = null;
        UpdateControls();
        MountFilesTabIfNeeded();
        if (TasksTabSelected)
        {
            if (_pollFinished is { } poll) await poll.Task;
            await PollTasksAsync();
        }
    }

    private void MountFilesTabIfNeeded()
    {
        if (!FilesTabSelected || !_mountOnFilesTab || _client is null || _busy || _closeRequested) return;
        _mountOnFilesTab = false;
        _ = RunAsync(MountSelectedAsync);
    }

    private void ResetTouchView()
    {
        _touchAvailable = true;
        ShowTouchSample(null);
    }

    private void ShowTouchSample(TouchSample? sample)
    {
        _touchDisplay.ShowSample(sample);
        _touchCoordinates.Text = sample is { Pressed: true }
            ? $"X: {sample.X}    Y: {sample.Y}    (АЦП: {sample.RawX}, {sample.RawY})"
            : "X: —    Y: —";
    }

    private async Task PollTouchAsync()
    {
        if (!TouchTabSelected || !_touchAvailable || _busy || _client is not { } client) return;
        await RunAsync(async () =>
        {
            try
            {
                var sample = await Task.Run(client.ReadTouch);
                if (TouchTabSelected) ShowTouchSample(sample);
            }
            catch (FatFsException ex) when (ex.Result is 6 or 19)
            {
                _touchAvailable = false;
                statusLabel.Text = "Обновите прошивку STM32 для работы с тачскрином.";
            }
        }, quiet: true);
        MountFilesTabIfNeeded();
    }

    private sealed class TouchDisplay : Control
    {
        private TouchSample? _sample;

        public TouchDisplay()
        {
            DoubleBuffered = true;
            ResizeRedraw = true;
            AccessibleName = "Область касания дисплея";
        }

        public void ShowSample(TouchSample? sample)
        {
            _sample = sample;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            float scale = Math.Min(ClientSize.Width / (float)TouchWidth, ClientSize.Height / (float)TouchHeight);
            if (scale <= 0) return;
            float width = TouchWidth * scale;
            float height = TouchHeight * scale;
            var screen = new RectangleF((ClientSize.Width - width) / 2, (ClientSize.Height - height) / 2, width, height);
            e.Graphics.FillRectangle(Brushes.Black, screen);
            if (_sample is not { Pressed: true } point) return;

            e.Graphics.SetClip(screen);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            float radius = Math.Max(2, 4 * scale);
            float x = screen.Left + (point.X + 0.5F) * scale;
            float y = screen.Top + (point.Y + 0.5F) * scale;
            e.Graphics.FillEllipse(Brushes.Red, x - radius, y - radius, radius * 2, radius * 2);
        }
    }
}
