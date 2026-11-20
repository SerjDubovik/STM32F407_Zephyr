using System.Diagnostics;

namespace SdCardBrowser;

internal sealed partial class BrowserForm
{
    private readonly TabPage _tasksTab = new("Потоки Zephyr") { UseVisualStyleBackColor = true };
    private readonly ListView _taskList = new()
    {
        AccessibleName = "Потоки Zephyr", View = View.Details, Dock = DockStyle.Fill,
        FullRowSelect = true, MultiSelect = false, HideSelection = false
    };
    private readonly Button _refreshTasks = new() { Text = "Обновить", AutoSize = true };
    private readonly Button _changeTask = new() { Text = "Приостановить", AutoSize = true };
    private readonly CheckBox _autoTasks = new()
    {
        Text = "Обновлять каждую секунду", AutoSize = true, Checked = true, Anchor = AnchorStyles.Left
    };
    private readonly Label _taskStatus = new()
    {
        Text = "Подключите плату для просмотра потоков Zephyr.", AutoSize = true, Dock = DockStyle.Fill
    };
    private readonly System.Windows.Forms.Timer _taskTimer = new() { Interval = 1000 };
    private bool _tasksAvailable = true;
    private List<RtosTask>? _previousTasks;
    private long _previousSampleTime;
    private bool TasksTabSelected => _workspaceTabs.SelectedTab == _tasksTab;
    private RtosTask? SelectedTask => _taskList.SelectedItems.Count == 1 ? _taskList.SelectedItems[0].Tag as RtosTask : null;

    private void InitializeTasksTab()
    {
        _workspaceTabs.TabPages.Add(_tasksTab);
        _taskList.Columns.Add("Поток", LogicalToDeviceUnits(140));
        _taskList.Columns.Add("Состояние", LogicalToDeviceUnits(140));
        _taskList.Columns.Add("Доля времени, %", LogicalToDeviceUnits(125), HorizontalAlignment.Right);
        _taskList.Columns.Add("Приоритет", LogicalToDeviceUnits(92), HorizontalAlignment.Right);
        _taskList.Columns.Add("Мин. свободно в стеке, байт", LogicalToDeviceUnits(210), HorizontalAlignment.Right);
        _taskList.Columns.Add("Управление", LogicalToDeviceUnits(115));

        var toolbar = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        toolbar.Controls.AddRange([_refreshTasks, _changeTask, _autoTasks]);
        var panel = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(6) };
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        panel.Controls.Add(toolbar, 0, 0);
        panel.Controls.Add(_taskList, 0, 1);
        panel.Controls.Add(_taskStatus, 0, 2);
        _tasksTab.Controls.Add(panel);

        _refreshTasks.Click += async (_, _) => await PollTasksAsync();
        _changeTask.Click += async (_, _) => await ChangeTaskAsync();
        _taskList.SelectedIndexChanged += (_, _) => UpdateTaskControls();
        _autoTasks.CheckedChanged += (_, _) => UpdateTaskControls();
        _taskTimer.Tick += async (_, _) => await PollTasksAsync();
        Disposed += (_, _) => _taskTimer.Dispose();
    }

    private void UpdateTaskControls()
    {
        bool connected = _client is not null && TasksTabSelected && _tasksAvailable;
        bool ready = connected && (!_busy || _pollFinished is not null);
        _refreshTasks.Enabled = ready;
        _autoTasks.Enabled = connected;
        _taskTimer.Enabled = connected && _autoTasks.Checked;
        _changeTask.Enabled = ready && SelectedTask is { Controllable: true };
        _changeTask.Text = SelectedTask?.State == "suspended" ? "Возобновить" : "Приостановить";
    }

    private void ResetTaskView()
    {
        _tasksAvailable = true;
        _previousTasks = null;
        _taskList.Items.Clear();
        _taskStatus.Text = "Подключите плату для просмотра потоков Zephyr.";
        UpdateTaskControls();
    }

    private async Task PollTasksAsync()
    {
        if (!TasksTabSelected || !_tasksAvailable || _busy || _closeRequested || _client is not { } client) return;
        await RunAsync(async () =>
        {
            try
            {
                var tasks = await Task.Run(client.ReadTasks);
                if (TasksTabSelected) ShowTasks(tasks);
            }
            catch (FatFsException ex) when (ex.Result is 6 or 19)
            {
                _tasksAvailable = false;
                _taskStatus.Text = "Для просмотра потоков установите прошивку STM32F407_Zephyr с командой tasks.";
            }
            catch (FatFsException ex)
            {
                _autoTasks.Checked = false;
                _taskStatus.Text = $"Не удалось получить список потоков. {ex.Message}";
            }
        }, quiet: true);
        MountFilesTabIfNeeded();
    }

    private async Task ChangeTaskAsync()
    {
        if (SelectedTask is not { Controllable: true } task || _client is not { } client) return;
        if (_pollFinished is { } poll) await poll.Task;
        if (!TasksTabSelected || _closeRequested || client != _client || SelectedTask?.Name != task.Name) return;
        bool resume = task.State == "suspended";
        await RunAsync(async () =>
        {
            await Task.Run(() => client.Execute(resume ? "resume" : "pause", task.Name));
            ShowTasks(await Task.Run(client.ReadTasks));
            statusLabel.Text = resume ? $"Поток {task.Name} возобновлён." : $"Поток {task.Name} приостановлен.";
        });
    }

    private void ShowTasks(List<RtosTask> tasks)
    {
        string? selected = SelectedTask?.Name;
        var load = new Dictionary<string, uint>(StringComparer.Ordinal);
        ulong total = 0;
        long now = Stopwatch.GetTimestamp();
        // A 32-bit microsecond counter wraps in just over 71 minutes.
        if (_previousTasks?.Count == tasks.Count &&
            Stopwatch.GetElapsedTime(_previousSampleTime, now) < TimeSpan.FromMinutes(70))
        {
            foreach (RtosTask task in tasks)
            {
                if (task.Runtime is not uint current ||
                    _previousTasks.Find(t => t.Name == task.Name)?.Runtime is not uint previous)
                {
                    total = 0;
                    break;
                }
                uint delta = unchecked(current - previous);
                load[task.Name] = delta;
                total += delta;
            }
        }

        _taskList.BeginUpdate();
        try
        {
            _taskList.Items.Clear();
            foreach (RtosTask task in tasks.OrderBy(t => t.Name, StringComparer.Ordinal))
            {
                string cpu = total > 0 ? (100d * load[task.Name] / total).ToString("F1") : "—";
                string state = task.State switch
                {
                    "running" => "Выполняется",
                    "ready" => "Готов",
                    "blocked" => "Ожидает",
                    "suspended" => "Приостановлен",
                    "deleted" => "Удалён",
                    _ => "Неизвестно"
                };
                var row = new ListViewItem([task.Name, state, cpu, task.Priority.ToString(), task.StackFree.ToString("N0"),
                    task.Controllable ? "Доступно" : "Только чтение"]) { Tag = task };
                _taskList.Items.Add(row);
                if (task.Name == selected) row.Selected = true;
            }
        }
        finally
        {
            _taskList.EndUpdate();
        }
        _previousTasks = tasks;
        _previousSampleTime = now;
        _taskStatus.Text = $"Потоков: {tasks.Count}   •   Меньшее число означает более высокий приоритет   •   {DateTime.Now:T}";
        UpdateTaskControls();
    }
}
