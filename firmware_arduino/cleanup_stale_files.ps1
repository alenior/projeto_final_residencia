<#
.SYNOPSIS
Remove arquivos obsoletos que a IDE Arduino compila automaticamente no sketch.

.DESCRIPTION
A IDE Arduino compila todo arquivo .cpp presente na pasta do sketch. Se sobrar uma
copia antiga com nome digitado errado, como irrigation_mananger.cpp, o linker passa
a encontrar duas implementacoes das mesmas funcoes publicas de Rega.

Execute este script a partir da raiz do repositorio ou da pasta firmware_arduino.
Opcionalmente informe -ArduinoSketchCache com a pasta exibida no log da IDE, por
exemplo C:\Users\alenior\AppData\Local\arduino\sketches\99673989521194DC62E9BC06025A7D2F.
#>
param(
  [string]$ArduinoSketchCache = ""
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectFirmwareDir = Resolve-Path $ScriptDir
$staleSketchFiles = @(
  "irrigation_mananger.cpp",
  "irrigation_mananger.h",
  "camera_manager.cpp"
)

Write-Host "[cleanup] Verificando pasta do firmware: $ProjectFirmwareDir"
foreach ($fileName in $staleSketchFiles) {
  $candidate = Join-Path $ProjectFirmwareDir $fileName
  if (Test-Path $candidate) {
    Remove-Item -Force $candidate
    Write-Host "[cleanup] Removido arquivo obsoleto do sketch: $candidate"
  }
}

if ($ArduinoSketchCache -ne "") {
  if (Test-Path $ArduinoSketchCache) {
    Remove-Item -Recurse -Force $ArduinoSketchCache
    Write-Host "[cleanup] Removido cache de build da IDE Arduino: $ArduinoSketchCache"
  } else {
    Write-Host "[cleanup] Cache informado nao encontrado: $ArduinoSketchCache"
  }
} else {
  Write-Host "[cleanup] Cache da IDE nao informado. Se o erro persistir, apague a pasta C:\Users\<usuario>\AppData\Local\arduino\sketches\<id>."
}

Write-Host "[cleanup] Concluido. Reabra a IDE Arduino e compile novamente."
