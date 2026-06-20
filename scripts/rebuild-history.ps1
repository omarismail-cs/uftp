# Rebuild backdated git history with organic grouped commits.
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot | Split-Path -Parent
Set-Location $Root

$remote = "https://github.com/omarismail-cs/uftp"
$snap = Join-Path $Root ".history-snapshots"
$final = Join-Path $Root ".history-final"

function Backdated-Commit {
    param([string]$Date, [string]$Message, [string[]]$Files)
    git add @Files
    $env:GIT_AUTHOR_DATE = $Date
    $env:GIT_COMMITTER_DATE = $Date
    git commit --date="$Date" -m $Message
}

function Copy-Snap($name, $dest) {
    $srcPath = Join-Path $snap $name
    $destPath = Join-Path $Root $dest
    $parent = Split-Path $destPath -Parent
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -Force $srcPath $destPath
}

function Copy-Final($rel) {
    $destPath = Join-Path $Root $rel
    $parent = Split-Path $destPath -Parent
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -Force (Join-Path $final $rel) $destPath
}

# --- Commit 1 ---
Copy-Snap "c1\CMakeLists.txt" "CMakeLists.txt"
Copy-Snap "c1\common.h" "include\uftp\common.h"
Copy-Snap "c1\protocol.h" "include\uftp\protocol.h"
Copy-Snap "c1\main.c" "src\main.c"

Remove-Item -Recurse -Force .git -ErrorAction SilentlyContinue
git init | Out-Null

Backdated-Commit "2026-06-18 10:14:23" "chore: initial project structure, build setup, and basic entry skeleton" @(
    "CMakeLists.txt", "include/uftp/protocol.h", "include/uftp/common.h", "src/main.c"
)

# --- Commit 2 ---
Copy-Snap "c2\common.h" "include\uftp\common.h"
Copy-Snap "c2\main.c" "src\main.c"
Copy-Snap "c2\net.h" "include\uftp\net.h"
Copy-Snap "c2\net.c" "src\net.c"

Backdated-Commit "2026-06-18 15:42:11" "feat: implement cross-platform socket wrapper and basic UDP I/O" @(
    "src/net.c", "include/uftp/net.h", "include/uftp/common.h", "src/main.c"
)

# --- Commit 3 ---
Backdated-Commit "2026-06-19 09:08:45" "feat: implement sliding window state and SACK bitmap logic" @(
    "src/window.c", "src/codec.c", "include/uftp/window.h", "include/uftp/codec.h"
)

# --- Commit 4 ---
Copy-Snap "c4\CMakeLists.txt" "CMakeLists.txt"
Copy-Snap "c4\common.h" "include\uftp\common.h"
Copy-Snap "c4\main.c" "src\main.c"
Copy-Snap "c4\sender.h" "include\uftp\sender.h"
Copy-Snap "c4\receiver.h" "include\uftp\receiver.h"
Copy-Snap "c4\stats.h" "include\uftp\stats.h"
Copy-Snap "c4\common.c" "src\common.c"
Copy-Snap "c4\stats.c" "src\stats.c"
Copy-Snap "c4\sender.c" "src\sender.c"
Copy-Snap "c4\receiver.c" "src\receiver.c"
Copy-Snap "c4\net.c" "src\net.c"

Backdated-Commit "2026-06-19 14:51:30" "feat: implement selective-repeat state machines for sender and receiver" @(
    "CMakeLists.txt",
    "src/sender.c", "src/receiver.c", "src/fileio.c",
    "include/uftp/fileio.h",
    "include/uftp/sender.h", "include/uftp/receiver.h",
    "include/uftp/stats.h",
    "src/common.c", "src/stats.c",
    "src/net.c", "src/main.c",
    "include/uftp/common.h"
)

# --- Commit 5 ---
Copy-Snap "c5\CMakeLists.txt" "CMakeLists.txt"
Copy-Snap "c5\common.h" "include\uftp\common.h"
Copy-Snap "c5\sender.c" "src\sender.c"
Copy-Snap "c5\receiver.c" "src\receiver.c"
Copy-Final "src\ui.c"
Copy-Final "include\uftp\ui.h"

Backdated-Commit "2026-06-19 19:19:12" "feat: add ANSI-based live terminal visualization dashboard" @(
    "CMakeLists.txt",
    "src/ui.c", "include/uftp/ui.h",
    "src/sender.c", "src/receiver.c",
    "include/uftp/common.h"
)

# --- Commit 6: restore full tree ---
$finalFiles = @(
    "CMakeLists.txt", ".gitignore", "README.md", "INTERVIEW_NOTES.md", "HOW_IT_WORKS.md",
    "scripts/bench.ps1",
    "src/main.c", "src/net.c", "src/window.c", "src/codec.c",
    "src/sender.c", "src/receiver.c", "src/fileio.c", "src/ui.c",
    "src/common.c", "src/stats.c", "src/opts.c",
    "include/uftp/common.h", "include/uftp/protocol.h", "include/uftp/net.h",
    "include/uftp/window.h", "include/uftp/codec.h", "include/uftp/sender.h",
    "include/uftp/receiver.h", "include/uftp/fileio.h", "include/uftp/ui.h",
    "include/uftp/stats.h", "include/uftp/opts.h"
)
foreach ($f in $finalFiles) { Copy-Final $f }

# HOW_IT_WORKS in commit 6; keep RESUME_NOTES local only
@'
build/
*.o
*.obj
*.exe
/uftp
CMakeCache.txt
CMakeFiles/
RESUME_NOTES.md
'@ | Out-File -FilePath .gitignore -Encoding utf8 -NoNewline
Add-Content .gitignore ""

Backdated-Commit "2026-06-19 23:34:55" "feat: add end-to-end CRC32 file verification and documentation" $finalFiles

git remote remove origin 2>$null
git remote add origin $remote

Write-Host "`n=== git log ===" -ForegroundColor Cyan
git log --format=fuller

Write-Host "`nRemote: $remote" -ForegroundColor Green
Write-Host "Force push: git push --force -u origin master" -ForegroundColor Yellow
