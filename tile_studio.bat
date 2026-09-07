@echo off
rem ---------------------------------------------------------------
rem  Tile Studio launcher  (tools/tile_studio.py)
rem  Starts the local server and opens the page in a browser.
rem  If the server is already up, it just opens the page.
rem  Keep this window open while you work; closing it stops the server.
rem ---------------------------------------------------------------
setlocal
cd /d "%~dp0"
set PORT=8770
set URL=http://127.0.0.1:%PORT%/

powershell -NoProfile -Command "try { $null = Invoke-WebRequest '%URL%api/refs' -UseBasicParsing -TimeoutSec 3; exit 0 } catch { exit 1 }"
if not errorlevel 1 (
  echo Tile studio is already running.  Opening %URL%
  start "" "%URL%"
  goto :eof
)

echo Starting tile studio on %URL%
echo Loading tile definitions takes about 10 seconds; the browser opens by itself.
echo Close this window to stop the server.
echo.
python tools/tile_studio.py --port %PORT%
if errorlevel 1 (
  echo.
  echo Failed to start.  Check that python is on PATH and that
  echo C:\Project\HengBand_Controler_Graf\batch is reachable.
  pause
)
