# The x64 MSVC environment (what a Developer Command Prompt sets) for the
# workflow steps that follow: the newest Visual Studio with the C++ tools
# runs its vcvars64.bat, and every variable that adds or changes goes to
# $GITHUB_ENV.
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'no Visual Studio with the C++ tools' }
$before = @{}
Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }
$after = cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64.bat failed' }
$changed = foreach ($line in $after) {
  $i = $line.IndexOf('=')
  if ($i -le 0) { continue }
  $name = $line.Substring(0, $i)
  $value = $line.Substring($i + 1)
  if ($before[$name] -cne $value) { "$name=$value" }
}
$changed | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
Write-Output "MSVC environment from $vs ($(@($changed).Count) variables)"
