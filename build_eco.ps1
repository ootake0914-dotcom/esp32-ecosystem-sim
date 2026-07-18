$ErrorActionPreference = "Stop"
$projectDir = "C:\Users\ootak\OneDrive\Desktop\esp32_gpio_serial\ecosystem_sim"
$buildDir = "$projectDir\build\esp32.esp32.esp32"
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$esptool = "C:\Users\ootak\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.3.0\esptool.exe"

Write-Host "--- COMPILING ECOSYSTEM SIM ---"
& $cli compile --clean --fqbn esp32:esp32:esp32 -e $projectDir

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compile failed! Exiting." -ForegroundColor Red
    exit 1
}

Write-Host "--- FLASHING ---"
& $esptool --no-stub --port COM19 --baud 460800 --chip esp32 write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 "$buildDir\ecosystem_sim.ino.bootloader.bin" 0x8000 "$buildDir\ecosystem_sim.ino.partitions.bin" 0x10000 "$buildDir\ecosystem_sim.ino.bin"

if ($LASTEXITCODE -ne 0) {
    Write-Host "Flash failed!" -ForegroundColor Red
    exit 1
}

Write-Host "--- DONE! ---" -ForegroundColor Green
