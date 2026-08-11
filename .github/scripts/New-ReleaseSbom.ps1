param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [Parameter(Mandatory = $true)]
    [string]$Commit,
    [string]$Version = "1.00"
)

$ErrorActionPreference = "Stop"
$document = [ordered]@{
    spdxVersion = "SPDX-2.3"
    dataLicense = "CC0-1.0"
    SPDXID = "SPDXRef-DOCUMENT"
    name = "WinMTR-Hyper-$Commit"
    documentNamespace = "https://github.com/HyperNetworkTech/WinMTR-Hyper/spdx/$Commit"
    creationInfo = [ordered]@{
        created = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
        creators = @("Tool: WinMTR-Hyper release workflow")
    }
    packages = @(
        [ordered]@{
            name = "WinMTR Hyper"
            SPDXID = "SPDXRef-Package-WinMTR-Hyper"
            versionInfo = $Version
            downloadLocation = "NOASSERTION"
            filesAnalyzed = $false
            licenseConcluded = "GPL-2.0-only"
            licenseDeclared = "GPL-2.0-only"
            copyrightText = "Copyright (C) 2010-2019 Appnor MSP S.A.; 2019-2026 Leetsoftwerx and contributors"
            supplier = "Organization: Hyper Network Technology LTD"
            sourceInfo = "Built from git commit $Commit; runtime dependencies are Windows system libraries recorded in IMPORTS.txt."
        }
    )
    relationships = @(
        [ordered]@{
            spdxElementId = "SPDXRef-DOCUMENT"
            relationshipType = "DESCRIBES"
            relatedSpdxElement = "SPDXRef-Package-WinMTR-Hyper"
        }
    )
}

$absoluteOutput = [IO.Path]::GetFullPath($OutputPath)
$directory = [IO.Path]::GetDirectoryName($absoluteOutput)
if ($directory) { [IO.Directory]::CreateDirectory($directory) | Out-Null }
$json = $document | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($absoluteOutput, $json + "`n", [Text.UTF8Encoding]::new($false))
