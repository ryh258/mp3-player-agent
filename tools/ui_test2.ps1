# 自动化 UI 测试 2：播放模式的真实行为
#   用 ListView 的"当前选中项"作为被观测状态
#   （LVM_GETNEXTITEM + LVNI_SELECTED，消息不带指针，可跨进程）
Add-Type -Namespace UI2 -Name T -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
'@

$ID_BTN_BASE  = 41010
$ID_LIST      = 41001
$ID_TIME_CUR  = 41005
$ID_PROGRESS  = 41002
$BTN_PREV = $ID_BTN_BASE + 2
$BTN_PLAY = $ID_BTN_BASE + 3
$BTN_NEXT = $ID_BTN_BASE + 5
$BTN_MODE = $ID_BTN_BASE + 6

$BM_CLICK   = 0x00F5
$WM_HSCROLL = 0x0114
$TBM_SETPOS = 0x0405
$LVM_GETNEXTITEM = 0x100C
$LVNI_SELECTED   = 0x0002
$TB_THUMBTRACK = 5
$TB_ENDTRACK   = 8

function Txt($h){ if($h -eq [IntPtr]::Zero){return "<null>"}; $sb=New-Object System.Text.StringBuilder 256; [void][UI2.T]::GetWindowTextW($h,$sb,256); return $sb.ToString() }
function Click($h){ [void][UI2.T]::SendMessageW($h,$BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero) }
function Sel($hList){ return [int]([UI2.T]::SendMessageW($hList,$LVM_GETNEXTITEM,[IntPtr](-1),[IntPtr]$LVNI_SELECTED)) }
function HScroll($m,$c,$code){ [void][UI2.T]::SendMessageW($m,$WM_HSCROLL,[IntPtr]$code,$c) }
function SeekTo($m,$tb,$permille){
  [void][UI2.T]::SendMessageW($tb,$TBM_SETPOS,[IntPtr]1,[IntPtr]$permille)
  HScroll $m $tb $TB_THUMBTRACK; Start-Sleep -Milliseconds 150; HScroll $m $tb $TB_ENDTRACK
}
function SetMode($bMode,$target){
  for($i=0;$i -lt 5;$i++){ if((Txt $bMode) -eq $target){ return $true }; Click $bMode; Start-Sleep -Milliseconds 350 }
  return ((Txt $bMode) -eq $target)
}
function GotoIndex($bNext,$bPrev,$hList,$want,$max){
  for($i=0;$i -lt $max;$i++){
    $s = Sel $hList
    if($s -eq $want){ return $true }
    if($s -lt $want){ Click $bNext } else { Click $bPrev }
    Start-Sleep -Milliseconds 350
  }
  return ((Sel $hList) -eq $want)
}

$global:pass = 0; $global:fail = 0
function Check($name,$cond,$detail){ if($cond){ Write-Host "  [通过] $name   $detail"; $global:pass++ } else { Write-Host "  [失败] $name   $detail"; $global:fail++ } }

$root = ".."
Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne 33884 } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600

# 测试会把 uitest_music 导进曲库，先备份，测完恢复
$libPath = "$root\data\library.txt"
$libBak  = "$root\build\library_uitest.bak"
if (Test-Path $libPath) { Copy-Item $libPath $libBak -Force }

# 让曲库只包含这 5 首等长测试曲，断言才有确定的意义
$uitest = Get-ChildItem "$root\build\uitest_music" -Filter *.wav | Sort-Object Name
[IO.File]::WriteAllLines($libPath, ($uitest | ForEach-Object { $_.FullName }), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "测试曲库已设为 $($uitest.Count) 首"

Write-Host "======== 测试 2：下一首 / 上一首 / 列表循环 / 单曲循环 / 随机 ========"
Start-Process "$root\MP3Player.exe" -ArgumentList "`"$root\build\uitest_music`"" -WorkingDirectory $root
# 轮询等主窗口出现（程序启动要读配置和曲库，固定 sleep 不可靠）
$p = $null
for($w=0; $w -lt 30 -and -not $p; $w++){
  Start-Sleep -Milliseconds 500
  $p = Get-Process MP3Player -ErrorAction SilentlyContinue |
       Where-Object { $_.MainWindowHandle -ne 0 } |
       Sort-Object StartTime -Descending | Select-Object -First 1
}
if(-not $p){ Write-Host "启动失败：等不到主窗口"; exit 1 }
Start-Sleep -Milliseconds 800
$m = [IntPtr]$p.MainWindowHandle
$hList = [UI2.T]::GetDlgItem($m,$ID_LIST)
$bNext = [UI2.T]::GetDlgItem($m,$BTN_NEXT)
$bPrev = [UI2.T]::GetDlgItem($m,$BTN_PREV)
$bMode = [UI2.T]::GetDlgItem($m,$BTN_MODE)
$tbProg = [UI2.T]::GetDlgItem($m,$ID_PROGRESS)

# v0.2 里文件夹参数是"导入曲库"，不会自动播；点头部「播放全部」开始
# （先点左侧导航第一项，确保停在"我的歌曲"视图 —— 头部按钮是随视图变的）
$hNav = [UI2.T]::GetDlgItem($m, 41030)
[void][UI2.T]::SendMessageW($hNav, 0x0201, [IntPtr]1, [IntPtr]((45 -shl 16) -bor 60))
Start-Sleep -Milliseconds 700
$bPlayAll = [UI2.T]::GetDlgItem($m,41040)
Click $bPlayAll
Start-Sleep -Seconds 2

Check "载入后从第 1 首开始" ((Sel $hList) -eq 0) "  选中项=$(Sel $hList)"

Click $bNext; Start-Sleep -Milliseconds 500
Check "点下一首 -> 第 2 首" ((Sel $hList) -eq 1) "  选中项=$(Sel $hList)"
Click $bNext; Start-Sleep -Milliseconds 500
Check "再点下一首 -> 第 3 首" ((Sel $hList) -eq 2) "  选中项=$(Sel $hList)"
Click $bPrev; Start-Sleep -Milliseconds 500
Check "点上一首 -> 回到第 2 首" ((Sel $hList) -eq 1) "  选中项=$(Sel $hList)"

# --- 列表循环：最后一首的下一首应回到第一首 ---
Check "切到 列表循环" (SetMode $bMode "列表循环") "  当前模式='$(Txt $bMode)'"
Check "走到最后一首(第5首)" (GotoIndex $bNext $bPrev $hList 4 12) "  选中项=$(Sel $hList)"
Click $bNext; Start-Sleep -Milliseconds 500
Check "列表循环下末首的下一首回到第 1 首" ((Sel $hList) -eq 0) "  选中项=$(Sel $hList)"

# --- 单曲循环：自然播完应重播同一首 ---
Check "切到 单曲循环" (SetMode $bMode "单曲循环") "  当前模式='$(Txt $bMode)'"
Check "回到第 1 首" (GotoIndex $bNext $bPrev $hList 0 12) "  选中项=$(Sel $hList)"
Start-Sleep -Milliseconds 500
SeekTo $m $tbProg 950          # 跳到 95%，1 秒内就会播完
Start-Sleep -Seconds 5
$s = Sel $hList
$t = Txt ([UI2.T]::GetDlgItem($m,$ID_TIME_CUR))
Check "单曲循环：播完后仍是同一首" ($s -eq 0) "  选中项=$s (期望 0)"
$sec = 99; if($t -match '^(\d+):'){ $sec = [int]$matches[1]*60 + [int]$matches[2] }
Check "单曲循环：播完后从头开始" ($sec -lt 6) "  当前时间=$t (期望 < 0:06)"

# --- 随机：点下一首应换一首，且索引合法 ---
Check "切到 随机播放" (SetMode $bMode "随机播放") "  当前模式='$(Txt $bMode)'"
$before = Sel $hList
$moved = $false
foreach($i in 1..6){ Click $bNext; Start-Sleep -Milliseconds 400; if((Sel $hList) -ne $before){ $moved = $true; break } }
$s = Sel $hList
Check "随机播放:点下一首换了曲目" ($moved -and $s -ge 0 -and $s -le 4) "  $before -> $s"

# --- 停止后仍有响应 ---
Click ([UI2.T]::GetDlgItem($m,$BTN_PLAY)); Start-Sleep -Milliseconds 800
Check "随机模式下暂停正常" ((Txt ([UI2.T]::GetDlgItem($m,$BTN_PLAY))) -eq '播放') "  按钮='$(Txt ([UI2.T]::GetDlgItem($m,$BTN_PLAY)))'"

Check "全程没崩" (@(Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq $p.Id }).Count -eq 1) "  进程仍在"

Get-Process MP3Player -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne 33884 } | Stop-Process -Force -ErrorAction SilentlyContinue
if (Test-Path $libBak) { Copy-Item $libBak $libPath -Force; Remove-Item $libBak -Force }
Write-Host "`n测试2 结果: 通过=$global:pass 失败=$global:fail"
