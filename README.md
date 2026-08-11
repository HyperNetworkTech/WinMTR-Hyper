# WinMTR v1.00 (Hyper Network Technology LTD)

WinMTR 是 Windows 圖形化網路診斷工具，將路由追蹤與持續 ICMP
延遲／丟包統計整合在同一個精簡介面。此版本以原始 WinMTR 為基礎，
完整採用繁體中文（台灣）介面，並改善 IPv4、IPv6、多路徑與匯出流程。

公司網站：[Hyper Network Technology LTD](https://hypernetwork.tw)

## 支援範圍

- Windows 7 SP1、Windows 8、Windows 8.1、Windows 10、Windows 11。
- 僅提供 x64（64 位元）版本，不支援 32 位元或 ARM64。
- 一般使用者權限即可執行。
- 核心路由追蹤不需要 Npcap、WinDivert、封包擷取驅動或額外執行階段。
- 使用 Windows 內建 ICMP API；目前不提供 TCP、UDP、SCTP、MPLS
  延伸資訊或原始封包擷取。

Windows 7 建議安裝 SP1、系統安全性更新、TLS 1.2 與最新根憑證。
若 HTTPS 或憑證功能過舊，路由追蹤仍可使用，但「目前網路資訊」的
外部查詢可能無法完成。

## 主要功能

- IPv4 與 IPv6 路由追蹤，可單獨或同時啟用。
- 以完整路徑為循環持續探測，停止時等候已送出的最後一批封包完成。
- 顯示丟包、已送、已收、最佳／平均／最差／最近延遲、抖動與標準差。
- 顯示國家、ASN 與 ISP；背景查詢不阻塞 ICMP 探測。
- 保留同一跳的 ECMP 多個回覆來源，畫面顯示上限與完整匯出彼此獨立。
- 將連續無回應跳數合併顯示，同時保留原始跳數資料。
- 節點詳細資料支援一般節點、ECMP 替代路徑與無回應區間。
- 可複製文字／HTML，或匯出 TXT、HTML、CSV、JSON。
- 可將完整程式視窗截圖至剪貼簿。
- 分享前檢查第一個實際探測跳數是否已累積 100 個封包。
- 保存追蹤選項與目標主機歷程；可單獨清除歷程。

## 基本操作

1. 執行 `WinMTR.exe`。
2. 在「主機」欄位輸入 IP 位址或主機名稱。
3. 視需要開啟「選項」調整間隔、封包大小、最大跳數與其他設定。
4. 按下「開始」，等候路由與統計資料累積。
5. 按兩下路由列可檢視節點詳細資料。
6. 使用「複製／匯出」或「截圖至剪貼簿」分享結果。
7. 按下「停止」時，程式會先處理已送出但尚未完成的探測。

診斷丟包問題時，建議至少等第一個實際探測跳數累積 100 個封包後再分享。

## 命令列

語法：

```text
WinMTR.exe [選項] "目標主機"
```

常用參數：

| 參數 | 用途 |
|---|---|
| `--interval VALUE`、`-i VALUE` | 設定探測間隔（秒） |
| `--size VALUE`、`-s VALUE` | 設定封包資料大小（位元組） |
| `--maxLRU VALUE`、`-m VALUE` | 設定目標主機歷程上限 |
| `--numeric`、`-n` | 本次啟動不解析主機名稱 |
| `--help`、`-h` | 顯示繁體中文說明 |

完整數值範圍、所有 GUI 等價 long options 與 process exit code 請見
[`docs/CLI.md`](docs/CLI.md)。

命令列指定的設定只覆蓋本次啟動所讀取的對應保存值。提供目標主機後，
程式會自動開始追蹤。Unicode、空白與引號會由 Windows Unicode 命令列處理；
含空白的目標請用雙引號包住。

範例：

```text
WinMTR.exe --interval 0.5 --size 64 "example.com"
WinMTR.exe -n "2001:db8::1"
```

## 目前網路資訊與隱私

預設會在啟動時查詢目前公網 IP、地區、ASN、網路業者、遞迴 DNS 與 ECS
狀態；可在「追蹤選項」關閉。查詢只用於顯示診斷資訊，不會變更 Windows
DNS 設定。

可能依成功狀況使用下列來源：

- IPv4：`ipinfo.io`，公網 IP 備援為 `api4.ipify.org`。
- IPv6：`v6.ipinfo.io`，公網 IP 備援為 `api6.ipify.org`。
- DNS／ECS：`whoami.ds.akahelp.net`。
- 地區與網路業者備援：`ipapi.co`。
- 節點與公網 ASN／ISP：ipinfo 為主要來源；單筆主要結果完全不可用時，才使用
  Team Cymru ASN 查詢服務或設定的次要備援，且不混合來源欄位。

詳細資料只列出本次實際成功使用的來源。本程式不使用
`edns.ip-api.com`，不偵測或要求 Google DNS，也不會把系統 DNS 改成
`8.8.8.8` 或 `8.8.4.4`。

## 建置

建議環境：

- Visual Studio 2022 Build Tools 或 Visual Studio 2022。
- 「使用 C++ 的桌面開發」、MFC、Windows 10/11 SDK。
- x64 編譯工具。
- CMake 3.28 以上（使用 CMake 時）。
- `vcpkg.json` 保留建置清單相容性；目前沒有第三方套件相依。

使用 Visual Studio 專案：

```powershell
msbuild WinMTR.sln /m /p:Configuration=Release /p:Platform=x64
```

使用 CMake：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

CMake 設定會拒絕 32 位元目標。專案使用靜態 MFC／CRT 的 Release 組態，
讓一般使用者不必另外安裝 Visual C++ 執行階段。

發布前應至少在 Windows 7 SP1、Windows 10 與 Windows 11 的 x64 環境完成
啟動、IPv4、IPv6、DPI 縮放、停止追蹤、剪貼簿與各匯出格式的實機驗證。
完整的自動、簽章與 VM gate 見 [`docs/RELEASE_CHECKLIST.md`](docs/RELEASE_CHECKLIST.md)。

## 維護與自訂

產品名稱、版本、公司、網址、字型及網路資訊來源集中在
[`WinMTRBranding.h`](WinMTRBranding.h)。完整位置與同步規則請參閱
[`CUSTOMIZATION.md`](CUSTOMIZATION.md)。

量測公式、匯出契約與安全政策分別記錄於
[`docs/STATISTICS.md`](docs/STATISTICS.md)、
[`docs/EXPORT_FORMATS.md`](docs/EXPORT_FORMATS.md) 與 [`SECURITY.md`](SECURITY.md)。

## 授權

本專案依 GNU General Public License version 2（GPL-2.0）授權；詳情請見
[`LICENSE`](LICENSE)。原始 WinMTR 與後續維護者的著作權及貢獻資訊保留於
原始碼與授權資料中。
