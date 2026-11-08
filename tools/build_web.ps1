# Windows wrapper for tools/build_web.py (gzips web/index.html into firmware/LED_Sign_Mesh/web_page.h)
$py = (Get-Command py, python3, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source; if (-not $py) { Write-Error 'python not found'; exit 1 }; & $py (Join-Path $PSScriptRoot 'build_web.py'); exit $LASTEXITCODE
