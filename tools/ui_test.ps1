# 自动化 UI 端到端测试：跨进程驱动播放器的按钮和滑条
#   - 按钮：SendMessage(hBtn, BM_CLICK) -> 会发 WM_COMMAND 给父窗口
#   - 滑条：先 TBM_SETPOS 设位置，再给父窗口发 WM_HSCROLL 模拟拖动
# 这样能真正走一遍用户点击时的那条代码路径。
Add-Type -Namespace UI -Name T -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, System.Text.StringBuilder lp);
'@

$ID_BTN_BASE = 41010
$ID_TIME_CUR = 41005
$ID_TIME_TOT = 41006
$ID_PROGRESS = 41002
$ID_VOLUME   = 41003
$BTN_PLAY = $ID_BTN_BASE + 3
$BTN_STOP = $ID_BTN_BASE + 4
$BTN_NEXT = $ID_BTN_BASE + 5
$BTN_PREV = $ID_BTN_BASE + 2
$BTN_MODE = $ID_BTN_BASE + 6

$BM_CLICK      = 0x00F5
$WM_HSCROLL    = 0x0114
$TBM_SETPOS    = 0x0405
$TBM_GETPOS    = 0x0400
$TB_THUMBTRACK = 5
$TB_ENDTRACK   = 8

function Txt($h){ if($h -eq [IntPtr]::Zero){return "<null>"}; $sb=New-Object System.Text.StringBuilder 256; [void][UI.T]::GetWindowTextW($h,$sb,256); return $sb.ToString() }
function Click($h){ [void][UI.T]::SendMessageW($h, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) }
function SetPos($h,$v){ [void][UI.T]::SendMessageW($h, $TBM_SETPOS, [IntPtr]1, [IntPtr]$v) }
function GetPos($h){ return [int]([UI.T]::SendMessageW($h, $TBM_GETPOS, [IntPtr]::Zero, [IntPtr]::Zero)) }
function HScroll($main,$ctl,$code){ [void][UI.T]::SendMessageW($main, $WM_HSCROLL, [IntPtr]$code, $ctl) }

$root = ".."
Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne 33884 } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

$global:pass = 0; $global:fail = 0
function Check($name, $cond, $detail){ if($cond){ Write-Host "  [通过] $name   $detail"; $global:pass++ } else { Write-Host "  [失败] $name   $detail"; $global:fail++ } }

# ================= 测试 1：单曲（长歌，便于观察） =================
Write-Host "======== 测试 1：暂停 / 继续 / 跳转 / 音量 / 模式 ========"
$mp3 = "$root\test_music\周杰伦 - 最伟大的作品_L.mp3"
Start-Process "$root\MP3Player.exe" -ArgumentList "`"$mp3`"" -WorkingDirectory $root
Start-Sleep -Seconds 3
$p = Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Sort-Object StartTime -Descending | Select-Object -First 1
$m = [IntPtr]$p.MainWindowHandle
$bPlay = [UI.T]::GetDlgItem($m,$BTN_PLAY)
$bStop = [UI.T]::GetDlgItem($m,$BTN_STOP)
$bNext = [UI.T]::GetDlgItem($m,$BTN_NEXT)
$bPrev = [UI.T]::GetDlgItem($m,$BTN_PREV)
$bMode = [UI.T]::GetDlgItem($m,$BTN_MODE)
$tbProg = [UI.T]::GetDlgItem($m,$ID_PROGRESS)
$tbVol  = [UI.T]::GetDlgItem($m,$ID_VOLUME)

$a = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
Start-Sleep -Seconds 2
$b = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
Check "播放中时间在走" ($a -ne $b) "  $a -> $b   按钮='$(Txt $bPlay)'"

# --- 暂停 ---
Click $bPlay; Start-Sleep -Milliseconds 800
$c = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
Start-Sleep -Seconds 2
$d = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
Check "点暂停后时间冻结" ($c -eq $d) "  $c -> $d   按钮='$(Txt $bPlay)'"
Check "暂停后按钮变'播放'" ((Txt $bPlay) -eq '播放') "  实际='$(Txt $bPlay)'"

# --- 继续 ---
Click $bPlay; Start-Sleep -Seconds 2
$e = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
Check "再点后恢复播放" ($e -ne $d) "  $d -> $e   按钮='$(Txt $bPlay)'"

# --- 拖进度条跳到 50% ---
SetPos $tbProg 500
HScroll $m $tbProg $TB_THUMBTRACK
Start-Sleep -Milliseconds 200
HScroll $m $tbProg $TB_ENDTRACK
Start-Sleep -Seconds 2
$f = Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))
$mm = 0; if($f -match '^(\d+):'){ $mm = [int]$matches[1] }
Check "拖动进度条跳到约 02:00" ($mm -ge 1 -and $mm -le 3) "  跳转后=$f (期望 2 分左右)"
Check "曲目总长显示 04:04" ((Txt ([UI.T]::GetDlgItem($m,$ID_TIME_TOT))) -eq '04:04') "  实际='$(Txt ([UI.T]::GetDlgItem($m,$ID_TIME_TOT)))'"

# --- 音量滑条 ---
SetPos $tbVol 30
HScroll $m $tbVol $TB_ENDTRACK
Start-Sleep -Milliseconds 500
Check "音量滑条可设到 30" ((GetPos $tbVol) -eq 30) "  实际=$(GetPos $tbVol)"

# --- 播放模式循环 ---
$modes = @()
foreach($i in 1..5){ $modes += (Txt $bMode); Click $bMode; Start-Sleep -Milliseconds 400 }
Check "模式按钮循环切换" ($modes[0] -ne $modes[1] -and $modes[0] -eq $modes[4]) "  $($modes -join ' -> ')"

# --- 停止 ---
Click $bStop; Start-Sleep -Seconds 1
Check "点停止后时间归零" ((Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR))) -eq '00:00') "  实际='$(Txt ([UI.T]::GetDlgItem($m,$ID_TIME_CUR)))'"

# --- 单曲顺序模式下点下一首应停止（不是崩溃） ---
Click $bNext; Start-Sleep -Seconds 1
Check "单曲时点下一首不崩溃" (@(Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq $p.Id }).Count -eq 1) "  进程仍在"

Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne 33884 } | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Host "`n测试1 结果: 通过=$pass 失败=$fail"