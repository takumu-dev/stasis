# Runs a command in the developer shell of the Visual Studio whose MSVC a CI job builds with, as a
# developer command prompt for MSVC would, with that Visual Studio's own CMake and Ninja first on
# the PATH:
#
#   ci/msvc-shell.ps1 -VisualStudio <2022|2026> [-Cl <version> -CMake <version> -Ninja <version>]
#       [-AddressSanitizer] [-- <command> [<argument>...]]
#
# The Visual Studio is the Build Tools of the year given, which CircleCI's Windows image has for
# both years. Given versions, the script first checks that cl, CMake and Ninja are Visual Studio's
# own and report those versions. With -AddressSanitizer, it checks that the shell has MSVC's
# AddressSanitizer runtime too, which msvc-asan links and runs with. It reports every promise the
# shell breaks rather than just the first, and runs the command only if none is broken.
#
# A step of a CircleCI job runs in a PowerShell of its own, so each step that needs the shell enters
# it again through this script. The script fails if anything does, the command included.
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory)]
    [ValidateSet('2022', '2026')]
    [string] $VisualStudio,
    [string] $Cl,
    [string] $CMake,
    [string] $Ninja,
    [switch] $AddressSanitizer,
    [Parameter(ValueFromRemainingArguments)]
    [string[]] $Command
)

$ErrorActionPreference = 'Stop'

$major = @{ '2022' = 17; '2026' = 18 }[$VisualStudio]
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installations = @(
    & $vswhere -products Microsoft.VisualStudio.Product.BuildTools `
        -version "[$major.0,$($major + 1).0)" -property installationPath
)
if ($installations.Count -ne 1) {
    throw "msvc-shell: Visual Studio $VisualStudio Build Tools should be installed once, but " +
        "vswhere found $($installations.Count) installations: $($installations -join ', ')"
}
$vs = $installations[0]

Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -Arch amd64 -HostArch amd64
Write-Host ("msvc-shell: Visual Studio $VisualStudio Build Tools is at $vs, with MSVC " +
    $env:VCToolsVersion)

# The developer shell puts Visual Studio's CMake and Ninja last on the PATH, after any others.
$cmakeTools = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:PATH = "$cmakeTools\CMake\bin;$cmakeTools\Ninja;$env:PATH"

$broken = 0

function Write-Broken([string] $promise) {
    Write-Host "msvc-shell: $promise"
    $script:broken++
}

# Checks that `program` on the PATH is Visual Studio's own, and that running it with `arguments`
# prints `version`. The version must stand apart from any other number, so that 1.12.1 doesn't pass
# for 1.12.10.
function Test-Version([string] $program, [string] $version, [string[]] $arguments) {
    $found = Get-Command $program -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) {
        Write-Broken "$program isn't on the PATH"
        return
    }
    if (-not $found.Source.StartsWith("$vs\", [StringComparison]::OrdinalIgnoreCase)) {
        Write-Broken "$program is $($found.Source), which isn't Visual Studio's own"
        return
    }
    # Windows PowerShell turns each line a program writes to stderr into an error, which would stop
    # the script, and cl writes its version there. The error holds the line.
    $ErrorActionPreference = 'Continue'
    $lines = & $found.Source @arguments 2>&1 | ForEach-Object {
        if ($_ -is [Management.Automation.ErrorRecord] -and $_.TargetObject -is [string]) {
            $_.TargetObject
        } else {
            "$_"
        }
    }
    $output = $lines -join "`n"
    $pattern = '(^|[^0-9.])' + [regex]::Escape($version) + '([^0-9.]|$)'
    if ($output -match $pattern) {
        Write-Host "msvc-shell: $program is $version, at $($found.Source)"
    } else {
        $invocation = (@($program) + $arguments) -join ' '
        Write-Broken "$program isn't ${version}: ``$invocation`` printed: $output"
    }
}

# Checks that the file `name` is in one of the directories that the environment variable
# `variable` lists.
function Test-OnSearchPath([string] $name, [string] $variable) {
    $directories = [Environment]::GetEnvironmentVariable($variable) -split ';' | Where-Object { $_ }
    $found = $directories | ForEach-Object { "$_\$name" } |
        Where-Object { [IO.File]::Exists($_) } | Select-Object -First 1
    if ($found) {
        Write-Host "msvc-shell: $name is at $found"
    } else {
        Write-Broken "$name isn't in any directory of $variable"
    }
}

if ($Cl) {
    Test-Version cl $Cl @()
}
if ($CMake) {
    Test-Version cmake $CMake @('--version')
}
if ($Ninja) {
    Test-Version ninja $Ninja @('--version')
}
# The runtime is a DLL that each program loads, through an import library it links.
if ($AddressSanitizer) {
    Test-OnSearchPath clang_rt.asan_dynamic-x86_64.dll PATH
    Test-OnSearchPath clang_rt.asan_dynamic-x86_64.lib LIB
}
if ($broken -ne 0) {
    throw "msvc-shell: promises broken: $broken"
}

if ($Command) {
    $program = $Command[0]
    $arguments = @($Command | Select-Object -Skip 1)
    # The command's exit code says whether it failed, not what it writes to stderr.
    $ErrorActionPreference = 'Continue'
    & $program @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "msvc-shell: ``$($Command -join ' ')`` failed with exit code $LASTEXITCODE"
    }
}
