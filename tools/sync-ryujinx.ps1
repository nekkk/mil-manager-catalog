param(
    [string]$CatalogUrl = "https://SEU_USUARIO.github.io/SEU_REPOSITORIO/index.json",
    [string]$MegaFolderUrl = "",
    [string]$RyujinxRoot = "$env:APPDATA\Ryujinx"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-Base64UrlToBytes {
    param([Parameter(Mandatory = $true)][string]$Value)

    $normalized = $Value.Replace('-', '+').Replace('_', '/')
    switch ($normalized.Length % 4) {
        2 { $normalized += '==' }
        3 { $normalized += '=' }
    }
    return [Convert]::FromBase64String($normalized)
}

function Parse-MegaFolderLink {
    param([Parameter(Mandatory = $true)][string]$Url)

    if ($Url -match '/folder/([^#]+)#(.+)$') {
        return @{
            FolderId = $Matches[1]
            FolderKey = $Matches[2]
        }
    }

    if ($Url -match '#F!([^!]+)!(.+)$') {
        return @{
            FolderId = $Matches[1]
            FolderKey = $Matches[2]
        }
    }

    throw "Link de pasta do MEGA invalido: $Url"
}

function Invoke-MegaApi {
    param(
        [Parameter(Mandatory = $true)][string]$FolderId,
        [Parameter(Mandatory = $true)][string]$Body
    )

    $uri = "https://g.api.mega.co.nz/cs?id=0&n=$FolderId"
    return Invoke-RestMethod -Method Post -Uri $uri -ContentType "application/json" -Body $Body -TimeoutSec 12
}

function New-AesTransform {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Transform
    )

    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Mode = $Mode
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::None
    $aes.Key = $Key
    if ($Mode -eq "CBC") {
        $aes.IV = [byte[]]::new(16)
    }

    if ($Transform -eq "Encrypt") {
        return @{
            Aes = $aes
            Transform = $aes.CreateEncryptor()
        }
    }

    return @{
        Aes = $aes
        Transform = $aes.CreateDecryptor()
    }
}

function Invoke-AesEcbDecrypt {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][byte[]]$Data
    )

    $ctx = New-AesTransform -Key $Key -Mode "ECB" -Transform "Decrypt"
    try {
        return $ctx.Transform.TransformFinalBlock($Data, 0, $Data.Length)
    } finally {
        $ctx.Transform.Dispose()
        $ctx.Aes.Dispose()
    }
}

function Invoke-AesCbcDecrypt {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][byte[]]$Data
    )

    $ctx = New-AesTransform -Key $Key -Mode "CBC" -Transform "Decrypt"
    try {
        return $ctx.Transform.TransformFinalBlock($Data, 0, $Data.Length)
    } finally {
        $ctx.Transform.Dispose()
        $ctx.Aes.Dispose()
    }
}

function Invoke-AesCtrCrypt {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Key,
        [Parameter(Mandatory = $true)][byte[]]$InitialCounter,
        [Parameter(Mandatory = $true)][byte[]]$Data
    )

    $ctx = New-AesTransform -Key $Key -Mode "ECB" -Transform "Encrypt"
    try {
        $counter = [byte[]]::new(16)
        [Array]::Copy($InitialCounter, 0, $counter, 0, [Math]::Min($InitialCounter.Length, 16))

        $output = [byte[]]::new($Data.Length)
        $offset = 0
        while ($offset -lt $Data.Length) {
            $keystream = $ctx.Transform.TransformFinalBlock($counter, 0, 16)
            $blockLength = [Math]::Min(16, $Data.Length - $offset)
            for ($index = 0; $index -lt $blockLength; $index++) {
                $output[$offset + $index] = $Data[$offset + $index] -bxor $keystream[$index]
            }

            for ($counterIndex = 15; $counterIndex -ge 0; $counterIndex--) {
                $counter[$counterIndex] = ($counter[$counterIndex] + 1) -band 0xFF
                if ($counter[$counterIndex] -ne 0) {
                    break
                }
            }

            $offset += $blockLength
        }
        return $output
    } finally {
        $ctx.Transform.Dispose()
        $ctx.Aes.Dispose()
    }
}

function Get-MegaFileName {
    param(
        [Parameter(Mandatory = $true)][string]$Attributes,
        [Parameter(Mandatory = $true)][byte[]]$FileKey
    )

    $encrypted = Convert-Base64UrlToBytes $Attributes
    $decrypted = Invoke-AesCbcDecrypt -Key $FileKey -Data $encrypted
    $text = [System.Text.Encoding]::UTF8.GetString($decrypted).Trim([char]0)
    if (-not $text.StartsWith("MEGA")) {
        return $null
    }

    $json = $text.Substring(4) | ConvertFrom-Json
    return [string]$json.n
}

function Get-MegaFileCryptoFromFolderEntry {
    param(
        [Parameter(Mandatory = $true)][byte[]]$FolderKey,
        [Parameter(Mandatory = $true)][string]$EntryKey
    )

    $separator = $EntryKey.IndexOf(':')
    $encodedNodeKey = if ($separator -ge 0) { $EntryKey.Substring($separator + 1) } else { $EntryKey }
    $encryptedNodeKey = Convert-Base64UrlToBytes $encodedNodeKey
    $decryptedNodeKey = Invoke-AesEcbDecrypt -Key $FolderKey -Data $encryptedNodeKey

    $fileKey = [byte[]]::new(16)
    for ($index = 0; $index -lt 16; $index++) {
        $fileKey[$index] = $decryptedNodeKey[$index] -bxor $decryptedNodeKey[$index + 16]
    }

    $iv = [byte[]]::new(16)
    [Array]::Copy($decryptedNodeKey, 16, $iv, 0, 8)

    return @{
        FileKey = $fileKey
        InitialCounter = $iv
    }
}

function Save-MegaFolderIndex {
    param(
        [Parameter(Mandatory = $true)][string]$FolderUrl,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    $link = Parse-MegaFolderLink -Url $FolderUrl
    $folderKey = Convert-Base64UrlToBytes $link.FolderKey

    $listResponse = Invoke-MegaApi -FolderId $link.FolderId -Body '[{"a":"f","c":1,"ca":1,"r":1}]'
    $nodes = @($listResponse[0].f)
    if (-not $nodes) {
        throw "A pasta do MEGA nao retornou arquivos."
    }

    $root = $nodes | Where-Object { $_.t -eq 1 } | Select-Object -First 1
    if (-not $root) {
        throw "Nao foi possivel identificar a raiz da pasta do MEGA."
    }

    $selected = $null
    foreach ($entry in $nodes) {
        if ($entry.t -eq 1 -or $entry.p -ne $root.h) {
            continue
        }

        $crypto = Get-MegaFileCryptoFromFolderEntry -FolderKey $folderKey -EntryKey ([string]$entry.k)
        $fileName = Get-MegaFileName -Attributes ([string]$entry.a) -FileKey $crypto.FileKey
        if (-not $selected) {
            $selected = @{
                Entry = $entry
                Crypto = $crypto
                Name = $fileName
            }
        }
        if ($fileName -and $fileName.ToLowerInvariant() -eq "index.json") {
            $selected = @{
                Entry = $entry
                Crypto = $crypto
                Name = $fileName
            }
            break
        }
    }

    if (-not $selected) {
        throw "Nenhum arquivo valido foi encontrado na pasta do MEGA."
    }

    $downloadBody = '[{"a":"g","g":1,"n":"' + $selected.Entry.h + '"}]'
    $downloadResponse = Invoke-MegaApi -FolderId $link.FolderId -Body $downloadBody
    $downloadUrl = [string]$downloadResponse[0].g
    if (-not $downloadUrl) {
        throw "A pasta do MEGA nao retornou URL de download para o indice."
    }

    $tempPath = [System.IO.Path]::GetTempFileName()
    try {
        Invoke-WebRequest -Uri $downloadUrl -OutFile $tempPath -TimeoutSec 20 | Out-Null
        $encryptedBytes = [System.IO.File]::ReadAllBytes($tempPath)
        $aesCtr = [System.Security.Cryptography.Aes]::Create()
        $aesCtr.Mode = [System.Security.Cryptography.CipherMode]::ECB
        $aesCtr.Padding = [System.Security.Cryptography.PaddingMode]::None
        $aesCtr.Key = $selected.Crypto.FileKey
        $encryptor = $aesCtr.CreateEncryptor()
        try {
            $counter = [byte[]]::new(16)
            [Array]::Copy($selected.Crypto.InitialCounter, 0, $counter, 0, 16)
            $plainBytes = [byte[]]::new($encryptedBytes.Length)

            for ($offset = 0; $offset -lt $encryptedBytes.Length; $offset += 16) {
                $keystream = $encryptor.TransformFinalBlock($counter, 0, 16)
                $blockLength = [Math]::Min(16, $encryptedBytes.Length - $offset)
                for ($index = 0; $index -lt $blockLength; $index++) {
                    $plainBytes[$offset + $index] = $encryptedBytes[$offset + $index] -bxor $keystream[$index]
                }

                for ($counterIndex = 15; $counterIndex -ge 0; $counterIndex--) {
                    $counter[$counterIndex] = ($counter[$counterIndex] + 1) -band 0xFF
                    if ($counter[$counterIndex] -ne 0) {
                        break
                    }
                }
            }
        } finally {
            $encryptor.Dispose()
            $aesCtr.Dispose()
        }

        $destinationDir = Split-Path -Parent $DestinationPath
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
        [System.IO.File]::WriteAllBytes($DestinationPath, $plainBytes)
    } finally {
        Remove-Item $tempPath -ErrorAction SilentlyContinue
    }
}

function Save-RemoteIndex {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if ($Url -match 'mega\.nz/(folder|#F!)') {
        Save-MegaFolderIndex -FolderUrl $Url -DestinationPath $DestinationPath
        return
    }

    $destinationDir = Split-Path -Parent $DestinationPath
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    Invoke-WebRequest -Uri $Url -OutFile $DestinationPath -TimeoutSec 20 | Out-Null
}

function Get-RyujinxGameVersion {
    param([Parameter(Mandatory = $true)][string]$TitleDir)

    $cpuCacheDir = Join-Path $TitleDir "cache\\cpu"
    if (-not (Test-Path $cpuCacheDir)) {
        return ""
    }

    $versions = Get-ChildItem $cpuCacheDir -Recurse -Filter *.cache -File |
        ForEach-Object {
            $parts = $_.BaseName.Split('-', 2)
            if ($parts[0] -and $parts[0].ToLowerInvariant() -ne "default") { $parts[0] }
        } |
        Sort-Object -Unique

    if (-not $versions) {
        return ""
    }

    return [string]($versions | Select-Object -Last 1)
}

function Export-RyujinxInstalledTitles {
    param(
        [Parameter(Mandatory = $true)][string]$RyujinxRootPath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    $gamesDir = Join-Path $RyujinxRootPath "games"
    $titles = @()
    if (Test-Path $gamesDir) {
        foreach ($child in Get-ChildItem $gamesDir -Directory | Sort-Object Name) {
            if ($child.Name -notmatch '^[0-9A-Fa-f]{16}$') {
                continue
            }

            $titles += [ordered]@{
                titleId = $child.Name.ToUpperInvariant()
                displayVersion = (Get-RyujinxGameVersion -TitleDir $child.FullName)
            }
        }
    }

    $payload = [ordered]@{
        generatedAt = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
        source = $gamesDir
        titles = $titles
    }

    $destinationDir = Split-Path -Parent $DestinationPath
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    $json = $payload | ConvertTo-Json -Depth 5
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($DestinationPath, $json, $utf8NoBom)
}

$switchDir = Join-Path $RyujinxRoot "sdcard\\switch\\mil_manager"
$indexPath = Join-Path $switchDir "index.json"
$installedPath = Join-Path $switchDir "emulator-installed.json"

$resolvedCatalogUrl = if ($MegaFolderUrl) { $MegaFolderUrl } else { $CatalogUrl }
Save-RemoteIndex -Url $resolvedCatalogUrl -DestinationPath $indexPath
Export-RyujinxInstalledTitles -RyujinxRootPath $RyujinxRoot -DestinationPath $installedPath

Write-Host "Catalogo sincronizado em: $indexPath"
Write-Host "Titulos exportados em: $installedPath"
