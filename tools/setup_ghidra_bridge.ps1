<#
.SYNOPSIS
    Instala e configura o ghidra-ai-bridge para o projeto LF2 Vita, ponta a ponta.

.DESCRIPTION
    Passos, todos idempotentes (re-rodar e' seguro):
      1. Preflight     - Python >= 3.10, winget, espaco em disco
      2. JDK 21        - exigido pelo Ghidra 11.3+/12.x
      3. Ghidra        - baixa o release mais recente se nao existir
      4. venv + pip    - instala ghidra-ai-bridge[headless] no .venv do projeto
      5. Projeto Ghidra- importa e analisa o lf2.exe (DEMORADO: 10-40 min)
      6. YAML          - escreve ghidra-bridge.yaml sem passar pelo wizard
      7. Export        - ghidra-bridge export all
      8. Smoke test    - decompila um endereco JA auditado e compara

    "Projeto Ghidra" aqui e' o .gpr - o banco de dados da ferramenta, nao um
    repositorio novo. Fica em reference/ghidra/, dentro deste mesmo repo.

    NAO roda `build-map`. O build-map constroi mapa endereco->funcao a partir de
    hooks/stubs no source. O nosso src/ e' porte: os enderecos la' sao comentario
    de evidencia, nao identidade de funcao. O mapa sairia vazio ou errado.

    ARQUIVO EM ASCII PURO, DE PROPOSITO. O Windows PowerShell 5.1 le' .ps1 como
    ANSI quando nao ha' BOM; qualquer caractere fora de ASCII quebra o parser com
    erros enganosos ("'}' de fechamento ausente"). Se for editar, mantenha ASCII.

.PARAMETER SkipAnalysis
    Pula o passo 5. Use quando o projeto Ghidra ja' existe e so' quer refazer
    config/export.

.PARAMETER GhidraDir
    Caminho de uma instalacao de Ghidra ja' existente. Se omitido, procura em
    C:\Ghidra\ghidra_*_PUBLIC e baixa se nao achar.

.PARAMETER MaxCpu
    Nucleos para o analyzeHeadless. Default 4.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\tools\setup_ghidra_bridge.ps1

.NOTES
    Escrito a partir da superficie documentada do ghidra-ai-bridge 0.2.0
    (README + PyPI). Nao foi executado. O unico ponto de ambiguidade na
    documentacao esta marcado com  # AJUSTE  abaixo.
#>

[CmdletBinding()]
param(
    [switch] $SkipAnalysis,
    [switch] $Reanalyze,
    [string] $GhidraDir = "",
    [int]    $MaxCpu = 4
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# PS 5.1 ainda negocia TLS 1.0 por padrao em algumas instalacoes; a API do
# GitHub recusa. Sem isto, o passo do download falha com erro de conexao.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------------------
#  Constantes do projeto
# ---------------------------------------------------------------------------
$ProjectRoot   = Split-Path -Parent $PSScriptRoot          # ...\LittleFighter2Vita
$ExeSource     = Join-Path $ProjectRoot 'reference\decomp\lf2.exe'
$GhidraProjDir = Join-Path $ProjectRoot 'reference\ghidra'
$GhidraProjNam = 'lf2'
$ProgramName   = 'lf2.exe'
$VenvDir       = Join-Path $ProjectRoot '.venv'
$ConfigPath    = Join-Path $ProjectRoot 'ghidra-bridge.yaml'
$ExportDir     = '.ghidra-exports'
$PreScript     = Join-Path $ProjectRoot 'tools\ghidra_pre_aggressive.py'
$ScriptPath    = Join-Path $ProjectRoot 'tools'
$GhidraRoot    = 'C:\Ghidra'

# Endereco de smoke test: saturacao do campo fall, achado A8, ja' confirmado no
# assembly. Se o bridge devolver algo coerente aqui, a instalacao presta.
$SmokeAddr     = '0x0042ea8c'

# ---------------------------------------------------------------------------
#  Saida
# ---------------------------------------------------------------------------
$script:StepNo = 0
function Step($msg) {
    $script:StepNo++
    Write-Host ""
    Write-Host "-- [$script:StepNo] $msg " -ForegroundColor Cyan -NoNewline
    Write-Host ("-" * [Math]::Max(4, 68 - $msg.Length)) -ForegroundColor DarkCyan
}
function Ok   ($msg) { Write-Host "  ok    $msg" -ForegroundColor Green }
function Skip ($msg) { Write-Host "  skip  $msg" -ForegroundColor DarkGray }
function Warn ($msg) { Write-Host "  AVISO $msg" -ForegroundColor Yellow }
function Info ($msg) { Write-Host "        $msg" -ForegroundColor DarkGray }
function Fail ($msg) { Write-Host ""; Write-Host "  ERRO  $msg" -ForegroundColor Red; exit 1 }

# Executa comando externo capturando stdout+stderr sem estourar.
# NECESSARIO: `2>&1` de executavel nativo transforma cada linha de stderr em
# ErrorRecord, e com $ErrorActionPreference='Stop' isso lanca excecao. Foi o que
# fez `java -version` (que escreve na stderr) retornar 0 em toda deteccao.
function Invoke-Capture($exe, $argList) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $raw = & $exe @argList 2>&1
        return (($raw | ForEach-Object { $_.ToString() }) -join "`n")
    } catch {
        return ''
    } finally {
        $ErrorActionPreference = $prev
    }
}

Write-Host ""
Write-Host "  ghidra-ai-bridge -> LF2 Vita" -ForegroundColor White
Write-Host "  projeto: $ProjectRoot" -ForegroundColor DarkGray

# ---------------------------------------------------------------------------
Step "Preflight"

if (-not (Test-Path $ProjectRoot)) { Fail "raiz do projeto nao encontrada: $ProjectRoot" }

if (-not (Test-Path $ExeSource)) {
    Write-Host ""
    Write-Host "  ERRO  lf2.exe nao encontrado em reference\decomp\." -ForegroundColor Red
    Write-Host "        Copie o binario original antes de rodar:" -ForegroundColor Red
    Write-Host "          copy `"C:\Program Files (x86)\LittleFighter\lf2.exe`" `"$ExeSource`"" -ForegroundColor Red
    exit 1
}
$exeHash = (Get-FileHash $ExeSource -Algorithm SHA256).Hash.Substring(0, 16)
Ok "lf2.exe encontrado (sha256:$exeHash...)"

# Python >= 3.10. Cada candidato e' um par exe+args; nada de fatiar array com
# indice invertido, que sob StrictMode vira erro de indice fora de faixa.
$pyExe  = $null
$pyArgs = @()
$cands  = @(
    @{ exe = 'py';     args = @('-3.13') },
    @{ exe = 'py';     args = @('-3.12') },
    @{ exe = 'py';     args = @('-3.11') },
    @{ exe = 'py';     args = @('-3.10') },
    @{ exe = 'python'; args = @()        }
)
foreach ($c in $cands) {
    if (-not (Get-Command $c.exe -ErrorAction SilentlyContinue)) { continue }
    $out = Invoke-Capture $c.exe @($c.args + '--version')
    if ($out -match 'Python (\d+)\.(\d+)') {
        if ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -ge 10) {
            $pyExe = $c.exe; $pyArgs = $c.args; break
        }
    }
}
if (-not $pyExe) { Fail "Python >= 3.10 nao encontrado. Instale: winget install Python.Python.3.12" }
Ok "Python: $pyExe $($pyArgs -join ' ')"

$drive = ($ProjectRoot.Substring(0, 1))
$free  = [Math]::Round((Get-PSDrive -Name $drive).Free / 1GB, 1)
if ($free -lt 8) { Warn "so' $free GB livres. Ghidra + projeto + exports pedem ~8 GB." }
else { Ok "espaco em disco: $free GB" }

# ---------------------------------------------------------------------------
Step "JDK 21 (exigido pelo Ghidra 11.3+)"

# O Ghidra e o PyGhidra procuram o JDK por JAVA_HOME, nao pelo PATH. Exigir
# `java` no PATH era erro do script: o instalador do winget muitas vezes nao
# mexe no PATH, e o usuario ficava preso num loop de "reabra o PowerShell".

function Get-JavaMajorFrom($javaExe) {
    if (-not $javaExe) { return 0 }
    if (-not (Test-Path $javaExe)) { return 0 }

    # Rota principal: o arquivo `release` na raiz do JDK. Nao roda processo
    # nenhum, entao nao depende de stderr nem de codigo de saida.
    # ($jdkRoot, nao $home: $HOME e' variavel automatica do PowerShell.)
    $jdkRoot = Split-Path -Parent (Split-Path -Parent $javaExe)
    $relFile = Join-Path $jdkRoot 'release'
    if (Test-Path $relFile) {
        $txt = Get-Content $relFile -Raw
        if ($txt -match 'JAVA_VERSION="(\d+)\.(\d+)') {
            $maj = [int]$Matches[1]
            if ($maj -eq 1) { return [int]$Matches[2] }   # "1.8.0_401" -> 8
            return $maj
        }
        if ($txt -match 'JAVA_VERSION="(\d+)"') { return [int]$Matches[1] }
    }

    # Fallback: rodar o java, agora com a captura segura.
    $out = Invoke-Capture $javaExe @('-version')
    if ($out -match 'version "(\d+)\.(\d+)') {
        $maj = [int]$Matches[1]
        if ($maj -eq 1) { return [int]$Matches[2] }
        return $maj
    }
    if ($out -match 'version "(\d+)"') { return [int]$Matches[1] }
    return 0
}

function Find-JdkOnDisk {
    $globs = @(
        'C:\Program Files\Microsoft\jdk-*\bin\java.exe',
        'C:\Program Files\Eclipse Adoptium\jdk-*\bin\java.exe',
        'C:\Program Files\Java\jdk-*\bin\java.exe',
        'C:\Program Files\Amazon Corretto\jdk*\bin\java.exe',
        'C:\Program Files\Zulu\zulu-*\bin\java.exe',
        'C:\Program Files\BellSoft\LibericaJDK-*\bin\java.exe',
        'C:\Program Files\RedHat\java-*\bin\java.exe',
        'C:\Program Files (x86)\Microsoft\jdk-*\bin\java.exe',
        (Join-Path $env:LOCALAPPDATA 'Programs\Eclipse Adoptium\jdk-*\bin\java.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Microsoft\jdk-*\bin\java.exe')
    )
    $hits = @()
    foreach ($g in $globs) {
        $found = Get-ChildItem $g -ErrorAction SilentlyContinue
        foreach ($f in $found) { $hits += $f.FullName }
    }
    return $hits
}

function Select-Jdk21 {
    $best = $null; $bestMaj = 0
    foreach ($j in (Find-JdkOnDisk)) {
        $m = Get-JavaMajorFrom $j
        if ($m -gt 0) { Info "encontrado: java $m  ($j)" }
        if ($m -ge 21 -and $m -gt $bestMaj) { $best = $j; $bestMaj = $m }
    }
    if ($best) { return @{ exe = $best; major = $bestMaj } }
    return $null
}

$jdk = $null

# 1) JAVA_HOME ja' apontando para algo bom?
if ($env:JAVA_HOME) {
    $cand = Join-Path $env:JAVA_HOME 'bin\java.exe'
    $m = Get-JavaMajorFrom $cand
    if ($m -ge 21) { $jdk = @{ exe = $cand; major = $m }; Ok "JAVA_HOME ja' aponta para java $m" }
    else { Warn "JAVA_HOME aponta para java $m (precisa 21+): $env:JAVA_HOME" }
}

# 2) PATH
if (-not $jdk) {
    $onPath = Get-Command java -ErrorAction SilentlyContinue
    if ($onPath) {
        $m = Get-JavaMajorFrom $onPath.Source
        if ($m -ge 21) { $jdk = @{ exe = $onPath.Source; major = $m }; Ok "java $m no PATH" }
        else { Warn "java $m no PATH - Ghidra 11.3+/12.x exige 21+. Procurando outro no disco..." }
    }
}

# 3) Varredura de disco (e' aqui que o JDK do winget aparece)
if (-not $jdk) {
    Info "procurando JDK instalado no disco..."
    $jdk = Select-Jdk21
    if ($jdk) { Ok "JDK $($jdk.major) encontrado fora do PATH" }
}

# 4) Instalar, e depois varrer o DISCO de novo - nao o PATH, que so' atualiza
#    em sessao nova. Era exatamente aqui que o script travava.
if (-not $jdk) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "nenhum JDK 21+ encontrado e winget indisponivel. Instale: https://learn.microsoft.com/java/openjdk/download"
    }
    Info "instalando Microsoft OpenJDK 21 via winget..."
    winget install --id Microsoft.OpenJDK.21 --accept-package-agreements --accept-source-agreements --silent
    Info "revarrendo o disco..."
    $jdk = Select-Jdk21
    if (-not $jdk) {
        Write-Host ""
        Write-Host "  ERRO  winget rodou mas nenhum JDK 21+ apareceu nos caminhos conhecidos." -ForegroundColor Red
        Write-Host "        Rode isto e me mostre a saida:" -ForegroundColor Red
        Write-Host "          winget list --query jdk" -ForegroundColor Red
        Write-Host "          Get-ChildItem 'C:\Program Files' -Filter '*jdk*' -Recurse -Depth 2 -Directory -EA SilentlyContinue | Select FullName" -ForegroundColor Red
        Write-Host "        Ou passe o caminho a mao:" -ForegroundColor Red
        Write-Host "          `$env:JAVA_HOME = 'C:\Program Files\Microsoft\jdk-21.0.5.11-hotspot'" -ForegroundColor Red
        exit 1
    }
    Ok "JDK $($jdk.major) instalado"
}

# JAVA_HOME e' a raiz do JDK: sobe dois niveis a partir de bin\java.exe
$JavaHome = Split-Path -Parent (Split-Path -Parent $jdk.exe)
$env:JAVA_HOME = $JavaHome
[Environment]::SetEnvironmentVariable('JAVA_HOME', $JavaHome, 'User')

# Poe o bin do JDK no PATH desta sessao (o Ghidra nao precisa, mas o
# analyzeHeadless.bat fica mais previsivel e `java -version` passa a funcionar).
$jdkBin = Join-Path $JavaHome 'bin'
if ($env:Path -notlike "*$jdkBin*") { $env:Path = "$jdkBin;$env:Path" }

Ok "JAVA_HOME = $JavaHome  (sessao + usuario)"

# ---------------------------------------------------------------------------
Step "Ghidra"

function Find-Ghidra($root) {
    if (-not (Test-Path $root)) { return $null }
    $hit = Get-ChildItem $root -Directory -Filter 'ghidra_*_PUBLIC' -ErrorAction SilentlyContinue |
           Sort-Object Name -Descending |
           Where-Object { Test-Path (Join-Path $_.FullName 'support\analyzeHeadless.bat') } |
           Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

if ($GhidraDir) {
    if (-not (Test-Path (Join-Path $GhidraDir 'support\analyzeHeadless.bat'))) {
        Fail "-GhidraDir '$GhidraDir' nao parece Ghidra (falta support\analyzeHeadless.bat)"
    }
    Ok "usando Ghidra informado: $GhidraDir"
} else {
    $found = Find-Ghidra $GhidraRoot
    if ($found) {
        $GhidraDir = $found
        Skip "Ghidra ja' instalado: $GhidraDir"
    } else {
        Info "resolvendo o release mais recente do Ghidra..."
        $rel = Invoke-RestMethod 'https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest' `
                                 -Headers @{ 'User-Agent' = 'lf2-setup' }
        $asset = $rel.assets | Where-Object { $_.name -like 'ghidra_*_PUBLIC_*.zip' } | Select-Object -First 1
        if (-not $asset) {
            Fail "nao achei o .zip no release $($rel.tag_name). Baixe manual: github.com/NationalSecurityAgency/ghidra/releases"
        }

        New-Item -ItemType Directory -Force -Path $GhidraRoot | Out-Null
        $zip = Join-Path $env:TEMP $asset.name
        if (Test-Path $zip) {
            Skip "zip ja' baixado: $zip"
        } else {
            Info "$($asset.name)  ($([Math]::Round($asset.size / 1MB)) MB) - alguns minutos"
            $oldProg = $ProgressPreference
            $ProgressPreference = 'SilentlyContinue'   # a barra do IWR custa ~10x
            try   { Invoke-WebRequest $asset.browser_download_url -OutFile $zip }
            finally { $ProgressPreference = $oldProg }
        }

        Info "extraindo..."
        # Expand-Archive no PS 5.1 leva varios minutos num zip deste tamanho.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $GhidraRoot)

        $GhidraDir = Find-Ghidra $GhidraRoot
        if (-not $GhidraDir) { Fail "extracao terminou mas nao achei analyzeHeadless.bat em $GhidraRoot" }
        Ok "Ghidra em $GhidraDir"
    }
}

$env:GHIDRA_INSTALL_DIR = $GhidraDir   # PyGhidra le' daqui
[Environment]::SetEnvironmentVariable('GHIDRA_INSTALL_DIR', $GhidraDir, 'User')
Ok "GHIDRA_INSTALL_DIR definido (sessao + usuario)"

# ---------------------------------------------------------------------------
Step "venv + ghidra-ai-bridge"

$VenvPy  = Join-Path $VenvDir 'Scripts\python.exe'
$VenvBin = Join-Path $VenvDir 'Scripts'

if (-not (Test-Path $VenvPy)) {
    Info "criando .venv..."
    & $pyExe @($pyArgs + @('-m', 'venv', $VenvDir))
    if (-not (Test-Path $VenvPy)) { Fail "venv nao foi criado em $VenvDir" }
    Ok ".venv criado"
} else {
    Skip ".venv ja' existe"
}

& $VenvPy -m pip install --upgrade pip --quiet

# ATENCAO: o pacote e' `ghidra-ai-bridge`. Existe no PyPI um `ghidra-bridge`
# COMPLETAMENTE DIFERENTE (ponte RPC Jython, outro autor). O executavel chama-se
# `ghidra-bridge` nos dois. Nao trocar o nome abaixo.
Info "pip install ghidra-ai-bridge[headless]..."
& $VenvPy -m pip install --upgrade "ghidra-ai-bridge[headless]"
if ($LASTEXITCODE -ne 0) { Fail "pip falhou" }

# O pyghidra do PyPI pode nao casar com a versao do Ghidra instalado. O proprio
# Ghidra distribui a wheel correta; ela ganha.
$wheel = Get-ChildItem (Join-Path $GhidraDir 'Ghidra\Features\PyGhidra\pypkg\dist\pyghidra-*.whl') -ErrorAction SilentlyContinue |
         Select-Object -First 1
if ($wheel) {
    Info "instalando o pyghidra que acompanha o Ghidra ($($wheel.Name))"
    & $VenvPy -m pip install --force-reinstall --no-deps $wheel.FullName --quiet
} else {
    Warn "wheel do pyghidra nao encontrada no Ghidra - usando a do PyPI"
}

$Bridge = Join-Path $VenvBin 'ghidra-bridge.exe'
if (-not (Test-Path $Bridge)) { $Bridge = Join-Path $VenvBin 'ghidra-bridge' }
if (-not (Test-Path $Bridge)) { Fail "executavel ghidra-bridge nao apareceu em $VenvBin" }

# Confirma que quem respondeu foi o pacote certo, nao o homonimo.
$shown = Invoke-Capture $VenvPy @('-m', 'pip', 'show', 'ghidra-ai-bridge')
if ($shown -notmatch 'Name:\s*ghidra-ai-bridge') {
    Fail "pacote errado instalado - veja o aviso sobre o homonimo no topo do script"
}
$ver = ([regex]::Match($shown, 'Version:\s*(\S+)')).Groups[1].Value
Ok "ghidra-ai-bridge $ver"

# ---------------------------------------------------------------------------
Step "Projeto Ghidra (import + analise)"

$gprPath = Join-Path $GhidraProjDir "$GhidraProjNam.gpr"

if ($Reanalyze -and (Test-Path $GhidraProjDir)) {
    Info "-Reanalyze: apagando $GhidraProjDir"
    Remove-Item $GhidraProjDir -Recurse -Force
}

if ($SkipAnalysis) {
    Skip "-SkipAnalysis"
    if (-not (Test-Path $gprPath)) { Warn "...mas $gprPath nao existe. O export vai falhar." }
} elseif (Test-Path $gprPath) {
    Skip "projeto ja' existe: $gprPath  (use -Reanalyze para refazer)"
} else {
    New-Item -ItemType Directory -Force -Path $GhidraProjDir | Out-Null
    $headless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'

    # NAO usar o nome $args: e' variavel automatica do PowerShell.
    $hlArgs = @($GhidraProjDir, $GhidraProjNam, '-import', $ExeSource, '-max-cpu', $MaxCpu)

    $usePre = Test-Path $PreScript
    if (-not $usePre) { Warn "tools\ghidra_pre_aggressive.py nao encontrado - analise padrao" }

    Warn "isto leva de 10 a 40 minutos e come RAM. Nao feche a janela."
    Write-Host ""

    $done = $false
    if ($usePre) {
        # O Ghidra 12 so' executa script Python sob PyGhidra. O analyzeHeadless.bat
        # cru responde "Ghidra was not started with PyGhidra. Python is not
        # available", ignora o -preScript e segue com a analise PADRAO - que foi
        # o que aconteceu na primeira execucao. Lancar pelo pyghidra do venv.
        Info "lancando via pyghidra (exigido pelo -preScript em Python)"
        $pgArgs = @('-m', 'pyghidra.ghidra_launch', '--install-dir', $GhidraDir,
                    'ghidra.app.util.headless.AnalyzeHeadless') +
                  $hlArgs + @('-preScript', 'ghidra_pre_aggressive.py', '-scriptPath', $ScriptPath)
        & $VenvPy @pgArgs
        if ($LASTEXITCODE -eq 0 -and (Test-Path $gprPath)) {
            $done = $true
            Ok "analise agressiva concluida"
        } else {
            Warn "pyghidra.ghidra_launch falhou (codigo $LASTEXITCODE)."
            Warn "Caindo para analyzeHeadless.bat SEM o preScript: menos funcoes."
            if (Test-Path $GhidraProjDir) { Remove-Item $GhidraProjDir -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $GhidraProjDir | Out-Null
        }
    }

    if (-not $done) {
        & $headless @hlArgs
        if ($LASTEXITCODE -ne 0) { Fail "analyzeHeadless retornou $LASTEXITCODE" }
        if (-not (Test-Path $gprPath)) { Fail "analise terminou sem gerar $gprPath" }
        Ok "projeto analisado (analise padrao)"
    }
}

# ---------------------------------------------------------------------------
Step "ghidra-bridge.yaml"

if (Test-Path $ConfigPath) {
    Copy-Item $ConfigPath "$ConfigPath.bak" -Force
    Info "config anterior salva em ghidra-bridge.yaml.bak"
}

# Escrito a mao em vez de `ghidra-bridge init`, que e' wizard interativo.
# Barras invertidas viram barras normais: em escalar YAML sem aspas o backslash
# e' literal, e duplicar/escapar aqui so' criaria caminho errado. O Ghidra e o
# Python aceitam '/' no Windows.
# hook_patterns/stub_patterns ficam de fora de proposito: sao para projeto de
# decomp com hooks (gta-reversed). Nosso src/ nao tem nada disso.
# code_range: lf2.exe e' PE 32-bit, image base 0x400000; a faixa cobre .text com
# folga (os enderecos auditados vao de 0x40xxxx a 0x450xxx).
$yaml = @"
# Gerado por tools/setup_ghidra_bridge.ps1 - reescrito a cada execucao.
# Este arquivo E' versionavel. O .ghidra-exports/ e o reference/ghidra/ nao.
ghidra:
  install_dir: $($GhidraDir -replace '\\', '/')
  project_dir: $($GhidraProjDir -replace '\\', '/')
  project_name: $GhidraProjNam
  program_name: $ProgramName

paths:
  export_dir: $ExportDir
  address_map: $ExportDir/address_map.json

source:
  root: ./src

binary:
  code_range_min: 0x00401000
  code_range_max: 0x00500000
"@
# Set-Content -Encoding UTF8 no PS 5.1 grava COM BOM, e o PyYAML le' o BOM como
# conteudo: "expected '<document start>', but found '<block mapping start>'".
# Gravar UTF-8 sem BOM, explicitamente.
[System.IO.File]::WriteAllText($ConfigPath, $yaml, (New-Object System.Text.UTF8Encoding($false)))
Ok "ghidra-bridge.yaml escrito"

# ---------------------------------------------------------------------------
Step "Export"

Push-Location $ProjectRoot
try {
    # AJUSTE: se o subcomando reclamar do argumento, tente `export` sem o `all`.
    # O README lista os tipos (all|structs|decompiled|vtables|globals|strings|
    # source-types) mas nao mostra a invocacao completa.
    Info "ghidra-bridge export all  (alguns minutos)"
    & $Bridge export all
    if ($LASTEXITCODE -ne 0) { Fail "export falhou (codigo $LASTEXITCODE)" }
    Write-Host ""
    & $Bridge info
    Ok "export concluido"
} finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
Step "Smoke test contra achado conhecido (A8)"

Push-Location $ProjectRoot
try {
    Info "containing $SmokeAddr  - deve cair na rotina de aplicacao de acerto"
    & $Bridge containing $SmokeAddr
    Write-Host ""
    Info "decompile $SmokeAddr"
    & $Bridge decompile $SmokeAddr
    Write-Host ""
    Warn "LEIA a saida acima. O campo fall deve saturar no piso da faixa"
    Warn "(60/40/20), nao acumular livre. Se o pseudocodigo disser outra coisa,"
    Warn "e' o Ghidra errando: Nivel B nao derruba Nivel A."
    Warn "Ver AUDITORIA_2026-08-12.md achado A8."
} finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
Step ".gitignore"

$giPath = Join-Path $ProjectRoot '.gitignore'
$needed = @('.ghidra-exports/', 'reference/ghidra/', '.venv/', 'ghidra-bridge.yaml.bak')
$gi = ''
if (Test-Path $giPath) { $gi = Get-Content $giPath -Raw }
$added = @()
foreach ($entry in $needed) {
    if ($gi -notmatch [regex]::Escape($entry)) { $added += $entry }
}
if ($added.Count -gt 0) {
    Add-Content $giPath ("`r`n# ghidra-ai-bridge (regeraveis)`r`n" + ($added -join "`r`n"))
    Ok "adicionado ao .gitignore: $($added -join ', ')"
} else {
    Skip ".gitignore ja' cobre tudo"
}

# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "-- pronto " -ForegroundColor Green -NoNewline
Write-Host ("-" * 62) -ForegroundColor DarkGreen
Write-Host ""
Write-Host "  Comandos (rode da raiz do projeto):" -ForegroundColor White
Write-Host ""
Write-Host "    .venv\Scripts\ghidra-bridge decompile 0x0042ea8c" -ForegroundColor Gray
Write-Host "    .venv\Scripts\ghidra-bridge xrefs-to  0x00417170   # os 264 sitios de RNG" -ForegroundColor Gray
Write-Host "    .venv\Scripts\ghidra-bridge pcode     0x0042e100" -ForegroundColor Gray
Write-Host "    .venv\Scripts\ghidra-bridge cfg       0x0042e100" -ForegroundColor Gray
Write-Host "    .venv\Scripts\ghidra-bridge context   0x0042ea8c   # bundle JSON" -ForegroundColor Gray
Write-Host ""
Write-Host "  Lembrete de precedencia:" -ForegroundColor Yellow
Write-Host "  o bridge entrega NIVEL B. Ele localiza a regiao e forma hipotese." -ForegroundColor DarkYellow
Write-Host "  A afirmacao continua saindo do assembly, com endereco." -ForegroundColor DarkYellow
Write-Host "  Ver tools/BINARY_NOTES.md e o cabecalho de tools/DECOMPILE.md." -ForegroundColor DarkYellow
Write-Host ""
