# 自訂字串與品牌位置說明

本說明整理維護者調整 WinMTR 品牌、繁體中文文字、預設值與外部來源時，
應修改的位置及同步規則。原則是：品牌、URL 與來源只改一個標頭、一般介面文字
只改資源、預設值只改設定常數，不在事件處理或匯出程式中新增散落字串。

## 品牌與固定資訊

主要位置：[`WinMTRBranding.h`](WinMTRBranding.h)

| 類別 | 常數／巨集 | 用途 |
|---|---|---|
| 顯示版本 | `WINMTR_BRAND_DISPLAY_VERSION`、`display_version` | 使用者看到的 `1.00` |
| 檔案版本 | `WINMTR_BRAND_FILE_VERSION`、`WINMTR_BRAND_FILE_VERSION_TEXT` | Windows 版本資源的 `1,0,0,0` 與 `1.0.0.0` |
| 產品名稱 | `WINMTR_BRAND_PRODUCT_NAME`、`product_name` | 主視窗、說明、匯出識別文字 |
| 公司名稱 | `WINMTR_BRAND_COMPANY_NAME`、`company_name` | 狀態列連結、關於資訊 |
| 公司連結 | `WINMTR_BRAND_COMPANY_URL`、`company_url` | `https://hypernetwork.tw` |
| 專案連結 | `WINMTR_BRAND_PROJECT_URL`、`project_url` | 專案原始碼與授權頁面連結 |
| 視窗標題 | `WINMTR_BRAND_*_WINDOW_TITLE` | 主視窗、追蹤選項、節點詳細資料、目前網路資訊、說明與授權視窗 |
| 一般字型 | `WINMTR_BRAND_UI_FONT`、`ui_font` | 台灣 Windows 中文介面字型 |
| 表格字型 | `WINMTR_BRAND_TABLE_FONT`、`table_font` | IP、主機名稱、ASN、ISP、表格與技術資料 |

`WinMTRBranding.h` 同時支援 C++ 與 Windows resource compiler：resource 使用
`WINMTR_*` 巨集，C++ 使用 `WinMTRBranding` 命名空間中的 `constexpr` 值。
請勿在其他檔案再複製產品名稱、版本、公司網址或字型名稱。

同一標頭的 `network_strings` 集中保存目前網路詳細資料的章節名稱、欄名、
「無法取得」與 ECS 狀態文字；其中需要同時出現在 resource 的值以
`WINMTR_UI_*` 巨集共用。`cli_strings` 則保存命令列剖析錯誤文字。

## 網路資訊來源

來源名稱、服務主機與 URL 也集中在 [`WinMTRBranding.h`](WinMTRBranding.h)
的 `WINMTR_SOURCE_*` 巨集及 `WinMTRBranding::sources` 命名空間。

| 類別 | 位置 | 用途 |
|---|---|---|
| IPv4 主要來源 | `IPINFO_IPV4_*` | `ipinfo.io` 連線資訊 |
| IPv6 主要來源 | `IPINFO_IPV6_*` | `v6.ipinfo.io` 連線資訊 |
| DNS／ECS | `AKAHELP_*` | `whoami.ds.akahelp.net` DNS 查詢 |
| 完整連線資訊備援 | `IPAPI_*` | `ipapi.co`；主要來源完全不可用時才查詢 |
| IPv4／IPv6 位址最終備援 | `IPIFY_IPV4_*`、`IPIFY_IPV6_*` | `api4.ipify.org`、`api6.ipify.org` |
| 指定位址 ASN／ISP 最終備援 | `TEAM_CYMRU_*` | Team Cymru IPv4、IPv6、ASN DNS 後綴與說明網址 |

同一個 IPv4、IPv6 或遞迴 DNS 中繼資料結果只採用一個可用來源，不以
其他服務補齊缺少欄位；只有目前來源完全無法產生可用結果時才依序查詢
備援。Windows 網路設定仍可用來列出本機 DNS 伺服器，但不列入「資料來源」。

新增或替換來源時，除了更新常數，也要同步檢查：

1. 來源成功紀錄只包含本次實際使用的服務。
2. IPv4 與 IPv6 連線路徑不被錯誤混用。
3. 逾時、取消、UTF-8、TLS 與 Windows 7 失敗降級仍可正常運作。
4. 不加入 `edns.ip-api.com`、Google DNS 偵測或任何自動變更 DNS 的行為。

## 介面、訊息與匯出文字

主要位置：[`WinMTR.rc`](WinMTR.rc) 的繁體中文對話框與 `STRINGTABLE`。

下列內容應建立資源 ID，並由 C++ 載入資源，不要直接寫在事件處理函式：

- 按鈕、標籤、欄位名稱與視窗標題。
- 狀態列文字、錯誤訊息、確認對話框與說明文字。
- 14 個路由表格欄名與節點詳細資料欄名。
- 分享樣本不足提醒、截圖成功／失敗訊息。
- 複製／匯出選單與檔案類型名稱。
- 目前網路資訊欄位、查詢中／失敗／無法取得及 ECS 狀態文字。
- TXT、HTML、CSV 的匯出欄名。

[`resource.h`](resource.h) 只保存資源 ID，不放可見文字。JSON 欄位名稱
（例如 `target`、`hops`、`responders`）屬於穩定的機器可讀格式，不應翻譯；
其餘匯出說明與資料值仍使用繁體中文（台灣）。

## 預設選項與有效範圍

主要位置：設定模型／[`WinMTRUtils.ixx`](WinMTRUtils.ixx) 的 `constexpr`
預設值與上下限。選項視窗、registry 載入驗證、命令列驗證及「恢復預設」
必須共同使用同一組常數。

調整設定時要同步確認：

- 設定模型的型別、預設值與範圍。
- 選項控制項及驗證訊息。
- registry 讀寫名稱與舊版設定遷移。
- `IWinMTROptionsProvider` 與追蹤工作階段快照。
- README 命令列說明（若既有 CLI 參數的意義或範圍有變更）。

命令列值只覆蓋本次啟動，不應直接寫回永久設定。

## Windows 版本資源與建置版本

版本與產品資訊的主要來源是 [`WinMTRBranding.h`](WinMTRBranding.h)。下列
位置必須在變更版本時一併檢查：

- [`WinMTR.rc`](WinMTR.rc)：`FILEVERSION`、`PRODUCTVERSION`、
  `FileVersion`、`ProductVersion`、`ProductName`、`FileDescription`。
- [`CMakeLists.txt`](CMakeLists.txt)：CMake 語法要求的 `project(WinMTR VERSION ...)`
  是獨立的純數字同步點。
- [`app.manifest`](app.manifest)：組件識別版本是另一個 XML
  純數字同步點。
- Visual Studio 專案的建置產物名稱（通常不含顯示版本）。

發布前請檢查檔案內容、主視窗標題與 Windows 檔案內容頁顯示相同版本，
且產品名稱為 `WinMTR v1.00 (Hyper Network Technology LTD)`。

## 相關檔案

- 使用者功能、平台、命令列與建置方式：[`README.md`](README.md)。
- GPL-2.0 授權全文：[`LICENSE`](LICENSE)。
- 本說明只整理維護位置；修改功能行為後仍要同步更新 README 與驗收紀錄。

## 發布前快速檢查

1. 使用 `rg -n "WinMTR-Refresh|Appnor's Free|MS Sans Serif"` 檢查是否殘留舊品牌或舊字型（著作權註記除外）。
2. 使用 `rg -n "edns\\.ip-api\\.com|8\\.8\\.8\\.8|8\\.8\\.4\\.4"` 確認禁止來源與 DNS 位址沒有進入功能程式。
3. 檢查所有可見文字為繁體中文（台灣），英文僅保留技術名稱、品牌與格式鍵名。
4. 以 x64 Release 建置，確認執行檔不需要額外 MFC／VC Runtime。
5. 在 Windows 7 SP1 到 Windows 11 實機或虛擬機驗證啟動、DPI、IPv4／IPv6、停止、剪貼簿與匯出。
