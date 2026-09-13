& "$PSScriptRoot\build.ps1"

Push-Location "$PSScriptRoot\.."

try
{
    ctest --preset x64-debug --output-on-failure
}
finally
{
    Pop-Location
}