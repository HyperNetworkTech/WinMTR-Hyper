# WinMTR-Hyper、WinMTR-refresh 與 mtr 完整實作比較及建議

> 比較日期：2026-08-12（Asia/Taipei）  
> 本文件是原始碼、建置檔、測試與文件的靜態稽核結果；「有功能」只代表比較 commit 中存在實作，不代表已在所有支援系統完成實機驗證。

## P0 實作追蹤（程式進度）

| 工作包 | 對應 P0 | 狀態 | 完成條件／證據 |
|---|---|---|---|
| 決定性排程核心與 fake clock 測試 | P02–P06、P23–P24、T01–T02、T11、G01–G03、X02 | 完成 | 中央排程器測試涵蓋 1s interval／3s timeout、silent 30 hops、slow reply、late reply、backpressure、10,000 次 epoch restart 及 10,000 次 start／stop；本機與 CI Debug／Release 全數通過 |
| 正確統計語意 | P12、S05–S06、Q16 | 完成 | `s_nethost`、UI 與 JSON 匯出已拆分 sent／completed／received／timed_out／in_flight／local_errors／scheduler_skipped／late_completions；Loss 僅為 timed_out／completed |
| Windows 探測 transport 與生命週期 | P08–P11、Q06、X01 | 完成 | 中央 scheduler + 有界 Windows thread pool；每 probe 穩定 request/buffer；完成佇列；stop/reset/restart drain |
| 全域／每 TTL hard cap | P04、P32、G34 | 完成 | logical 與 transport 雙重上限、每 TTL 上限及 backpressure 已接入 Windows transport；資源不足不計網路遺失 |
| TTL 均勻 spacing | G02 | 完成 | 生產 scheduler 與測試均在 interval 內等距配置 TTL slot，後續每 TTL 維持獨立 send-to-send cadence |
| 預設 timeout 3,000 ms | P07 | 完成 | `WinMTRUtils::DEFAULT_TIMEOUT_MS == 3000`；option snapshot、registry 缺省值、UI 與 CLI 共用同一常數 |
| 每完成事件更新、UI 50–100ms 合併 | P03、P24（UI 可觀察性） | 完成 | 每筆 issue／reply／timeout／local error 更新 model；lock-free dirty flag 由 100ms dialog timer 合併重繪 |
| Windows CI 與壓力驗證 | T10–T11 | 完成 | [Windows Build run 31529986318](https://github.com/HyperNetworkTech/WinMTR-Hyper/actions/runs/31529986318) 的 x64 Debug／Release build + CTest 全部通過；另有兩組各 10,000 次生命週期決定性測試 |

狀態定義：只有在程式已接入且對應測試／建置實際通過後才標為「完成」；僅有程式初稿一律維持「進行中」。

## P1 實作追蹤（更新至 `43afd28`）

| 工作包 | 狀態 | 完成條件／目前證據 |
|---|---|---|
| 探測 outcome、排程診斷、route ceiling／frontier／hysteresis | 完成 | 生產 tracing loop 共用純 C++ scheduler／route policy；scripted 測試涵蓋 8→12→6、silent destination、unknown tail、minimum TTL |
| DNS 解析與雙棧候選 | 完成 | 5 秒 deadline／取消；Windows 8+ 動態解析 async cancel、Win7 bounded fallback；IPv4/IPv6 候選 250 ms 交錯競賽 |
| Metadata 併發、cache、來源與輸入界線 | 完成 | bounded `jthread` pool／queue、正負 TTL、provider cooldown/backoff、總 query budget、ipinfo 主源、不混欄位、1 MiB body／4 KiB JSON string／欄位截斷 |
| UI 增量更新、unknown folding、DPI／高對比、內容尺寸 | 程式完成／實機矩陣待驗證 | keyed row/cell diff、等待與無回覆折疊、終點去重、手動 resize 保護均已接入；仍需 96/120/144/192 DPI 與高對比 VM 截圖 |
| CLI、設定 migration、grace 與全域 PPS | 完成 | GUI 等價 long options、strict parse、exit code 2、settings schema v2、bounded grace drain、fair global rate cap |
| TXT／HTML／CSV／JSON 匯出 | 核心完成／完整 golden matrix 待補 | 原子替換、Win32 error、CSV formula 防護、JSON schema v1、Unicode/null/ECMP golden；尚欠四種格式由同一 fixture 直接產生的完整 golden matrix |
| ECMP responder 獨立統計 | 完成 | stable ID／LRU／hit count，加上每 responder best/avg/worst/last、sample stddev、recent/EWMA jitter；UI/CSV/JSON 已接入 |
| Windows build／CI／文件 | 完成 | checked-in solution + CMake x64 Debug/Release、`/W4 /WX /sdl`、nightly ASAN、schema/import/asInvoker/checksum/SPDX SBOM gate、SECURITY/CHANGELOG/CLI/STATISTICS/RELEASE_CHECKLIST 文件 |
| Authenticode、Windows 7/10 VM、完整 UI screenshot matrix | 外部 gate | 需要受保護簽章憑證與對應 VM／互動桌面；程式或 CI 閘門可準備，但不得在沒有憑證／實測紀錄時標示完成 |

下方各列的「實作狀態」以目前工作樹校正；「比較 commit」欄仍刻意保留最初稽核基準，方便檢視修改前後差異。

## 1. 比較基準與方法

| 專案 | 本報告名稱 | 比較 commit | commit 時間 | 語言／主要介面 | 追蹤檔案 | 生產程式規模（註） |
|---|---|---|---|---|---:|---:|
| [HyperNetworkTech/WinMTR-Hyper](https://github.com/HyperNetworkTech/WinMTR-Hyper/tree/42460157cf41c251f2496041732ca17b8f16f89d) | **Hyper** | [`42460157cf41c251f2496041732ca17b8f16f89d`](https://github.com/HyperNetworkTech/WinMTR-Hyper/commit/42460157cf41c251f2496041732ca17b8f16f89d) | 2026-08-12 02:44:47 +08:00 | C++23 named modules、MFC、Win32 API | 55 | 37 個 `.cpp/.ixx/.h/.rc`，約 8,463 行 |
| [leeter/WinMTR-refresh](https://github.com/leeter/WinMTR-refresh/tree/932c8133a6973a2fa44fbcbc1cf066717b929748) | **Refresh** | [`932c8133a6973a2fa44fbcbc1cf066717b929748`](https://github.com/leeter/WinMTR-refresh/commit/932c8133a6973a2fa44fbcbc1cf066717b929748) | 2026-06-08 17:40:06 -06:00 | C++20 named modules、MFC、C++/WinRT | 51 | 34 個 `.cpp/.ixx/.h/.rc`，約 4,812 行 |
| [traviscross/mtr](https://github.com/traviscross/mtr/tree/7b017733aef06bb3d8e3573b2e964cc876644fad) | **mtr** | [`7b017733aef06bb3d8e3573b2e964cc876644fad`](https://github.com/traviscross/mtr/commit/7b017733aef06bb3d8e3573b2e964cc876644fad) | 2026-06-16 11:03:47 +02:00 | C、CLI／curses／GTK、`mtr-packet` | 128 | 59 個非測試 `.c/.h`，約 14,027 行；另有約 2,113 行測試／fuzz 程式 |

註：行數是為比較維護量而以相同規則計算的實體行數，不是複雜度、品質或功能數量的評分。Hyper 與 Refresh 有 51 個同名追蹤檔案；Hyper 另外增加 `WinMTRBranding.h`、`WinMTRNetworkData.cpp/.h`、`CUSTOMIZATION.md`。

### 判讀標記

| 標記 | 意義 |
|---|---|
| ✅ | 有完整或可用實作 |
| ◐ | 有部分實作、條件式建置，或語意和需求不完全相同 |
| — | 沒有該功能 |
| ⚠ | 有實作，但本次稽核發現正確性、生命週期、相容性或維護風險 |
| **自訂** | 三者都不適合直接採用；本報告提出適合 Hyper 的第四種設計 |

### 優先級

| 優先級 | 定義 |
|---|---|
| **P0** | 會改變量測正確性、封包節奏、遺失率，或可能造成非同步記憶體／生命週期錯誤；應先於新增功能處理 |
| **P1** | 下一個正式版本應完成的可靠性、相容性或核心產品能力 |
| **P2** | 明顯提升診斷能力、可維護性或使用體驗 |
| **P3** | 進階／利基能力，可在核心穩定後排程 |
| **保留** | 現行設計較佳，只需補測試、文件或小幅整理 |

## 2. 總結：本修改版應採用哪一套設計

| 層次 | 最佳來源 | 結論 | 對 Hyper 的具體方向 | 優先級 |
|---|---|---|---|---|
| Windows 產品介面、在地化、匯出、公網資訊 | **Hyper** | 三者中 Hyper 最完整，也最符合本修改版定位 | 保留現有 MFC UI、繁體中文、14 欄、節點／公網詳細資料、ECMP 展開、內容自動縮放與多格式匯出；把它們和探測核心進一步解耦 | 保留 |
| 探測排程語意 | **mtr** | `mtr` 將「送出週期」和「單一探測 timeout」完全分離；這正是 MTR 類工具應有的行為 | 依 `mtr` 的 pending-probe／deadline 思路重寫 Windows 原生排程器，不直接移植 Unix raw socket 程式 | 完成 |
| Windows 非同步 ICMP 實作 | **自訂** | Hyper 的同步 round 會讓 timeout 和週期耦合；Refresh 雖為非同步，但 Windows async ICMP 的 `Timeout` 不生效，且手動 5 秒喚醒後的 request/buffer/handle 生命週期不夠安全 | 建立穩定配置的 `ProbeRequest`、每包 token、事件完成佇列、deadline min-heap、late-reply 丟棄規則與停止時 drain；所有 buffer/event/handle 活到 OS 完成通知後才釋放 | 完成 |
| 路徑／ECMP 資料模型 | **Hyper + mtr** | Hyper 已有 128 responders、顯示上限、metadata 與完整匯出；mtr 的 pending correlation、路徑判定與成熟行為更佳 | 保留 Hyper responder metadata；改採 mtr 式序號關聯及連續探測，另外增加「每個 responder 的獨立統計」 | P1 |
| 統計演算法 | **mtr + 自訂** | mtr 的 sample standard deviation、幾何平均、多種 jitter 與 400 點歷史較完整；Hyper 使用 64-bit/Welford 較安全 | 保留 Hyper 64-bit 與 Welford，將標準差改為明確的樣本標準差；增加 jitter current/avg/max/RFC1889、可選滑動視窗與 percentile | P1/P2 |
| 安全權限模型 | **Hyper（ICMP）／mtr（未來 raw protocols）** | 只用 Windows ICMP API 時 Hyper 的 `asInvoker` 最簡潔安全；加入 raw TCP/UDP/MPLS 後才需要隔離 | ICMP 永遠維持無管理員權限；若未來加 raw packet，另建最小權限 helper process，仿 mtr 的 UI／packet 分離，不讓 MFC 主程式提升權限 | 保留/P2 |
| 測試與模糊測試 | **mtr** | mtr 明顯領先；Hyper/Refresh 沒有可執行測試，CI 也不會編譯 Windows 專案 | 抽象 `IProbeTransport` 與 clock，先建立 deterministic scheduler tests，再補封包解析 fuzz、Windows build matrix 與實機 smoke tests | **P0/P1** |
| 建置與相容性 | **Hyper + 自訂** | Hyper 的 Windows 7 desktop API 面較合適，但 CMake「只支援 x64」和 `.sln/.vcxproj` 仍列 Win32/ARM64 有設定漂移 | 決定唯一支援矩陣；若確定只發 x64，就刪除或禁止其他 VS 組態；若要 ARM64，則正式納入 CMake、CI 與實機驗證 | P1 |

最重要的結論是：**不要以把 timeout 降到 1 秒來換取每秒更新。** 正確架構應允許每一秒繼續送新包，同時讓先前的包保留 3 秒或 10 秒等待回覆。`interval` 是發送排程，`timeout` 是每個 request 的獨立 deadline，兩者不可取較小值合併。

## 3. 架構、平台、建置、相依性與授權

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| A01 | 產品型態 | 原生 Windows GUI，繁中品牌版 | 原生 Windows GUI，接近上游 WinMTR | Unix CLI／TUI／GTK；Windows 主要經 WSL/Cygwin | Hyper | 維持原生 GUI 定位；mtr 只作核心語意與測試參考 | 保留 | 完成（既有） |
| A02 | 語言 | C++23 | C++20 | C | Hyper | C++23 可保留，但排程核心避免依賴 UI/MFC，使純 C++ 測試可在 CI 執行 | P1 | 完成|
| A03 | 模組系統 | named modules + module partitions | named modules + module partitions | 傳統 `.c/.h` | Hyper | 保留 modules；為測試建立明確 public interfaces，避免私有 partition 承擔全部邏輯 | P2 | 未開始 |
| A04 | GUI framework | MFC/Win32 | MFC/Win32 | curses、GTK、文字報告 | Hyper | MFC 符合目標；不要為了共用 mtr UI 而引入 GTK | 保留 | 完成（既有） |
| A05 | 核心分層 | Dialog、Net、NetworkData 已分檔，但 `WinMTRNet-Tracing.cpp` 同時含 transport、scheduler、path、metadata 啟動 | 類似，但 coroutine/ICMP wrapper 分檔 | UI/control 與 `mtr-packet` 清楚分離 | mtr | 拆成 `ProbeTransport`、`ProbeScheduler`、`RouteModel`、`MetadataResolver`；Dialog 只訂閱 snapshot | **P0/P1** | 部分完成（scheduler／route policy／packet parameters 已拆）|
| A06 | process 邊界 | 單一非提升權限 process | 單一非提升權限 process | `mtr` + 最小權限 `mtr-packet` helper | 依協定而定 | ICMP 保持單 process；只有新增 raw socket 協定時才引入受限 helper | P2 | 未開始 |
| A07 | 建置系統 | CMake 3.28 + VS solution/project | CMake 3.10 + VS solution/project | Autotools/Automake | Hyper | Windows 專案以 CMake 為權威，生成 VS 專案或在 CI 比對手寫 `.vcxproj`，避免雙重真相 | P1 | 完成|
| A08 | CMake 工具鏈限制 | 明確拒絕非 Windows、非 MSVC、非 x64 | 沒有相同嚴格檢查 | 廣泛 Unix/Cygwin feature detection | Hyper | 保留錯誤訊息；支援矩陣若改變，CMake 與 README 同一 commit 更新 | 保留 | 完成（既有） |
| A09 | VS 平台組態 | `.sln/.vcxproj` 仍含 Win32、x64、ARM64，與 CMake/README「僅 x64」矛盾 | 同樣含多平台組態 | 由 configure 偵測 host | 自訂 | 先決定產品政策；目前建議刪除 Win32/ARM64 組態或讓它們 fail-fast，不可顯示成可建置但未驗證 | P1 | 完成|
| A10 | C++ 標準一致性 | CMake `C++23`；VS project 為 `stdcpplatest` | CMake `C++20`；VS project 為 `stdcpplatest` | C compiler 由 configure 選擇 | 自訂 | VS project 固定 `/std:c++23`，不使用浮動 latest，避免新 VS 更新改變 ABI/語意 | P1 | 工具鏈限制（CMake 固定 C++23；VS 17.14 僅提供 stdcpplatest）|
| A11 | Windows API 基線 | `_WIN32_WINNT=0x0601`，desktop libraries，Windows 7 fallback | 最新 SDK/OneCore/WinRT，manifest 偏 Windows 10/11 | Unix API；Cygwin 有專用 backend | Hyper | 若 Windows 7 是正式承諾，維持 desktop API；新增 API 一律 dynamic resolve + fallback | 保留 | 完成（既有） |
| A12 | DPI manifest | Per-Monitor V2，舊系統回退 | manifest 未見同級完整 per-monitor 設計 | terminal/GTK 各自處理 | Hyper | 保留；CI 加 100/125/150/200% screenshot/layout 測試 | P1 | 程式完成／DPI VM 截圖待外部驗證|
| A13 | 執行權限 | `asInvoker`，Windows ICMP API 不需 driver/admin | `asInvoker` | raw socket 需 capability/setuid；啟動後永久 drop | Hyper | ICMP 不要求 UAC；未來需要權限的功能單獨隔離並清楚標示 | 保留 | 完成（既有） |
| A14 | CRT/MFC 發布 | CMake Release 靜態 CRT/MFC；VS 另有 static/dynamic 組態 | 同樣混合多組態 | 動態依賴依平台 | Hyper | 發布 pipeline 只允許經驗證的 static x64 Release artifact，並檢查 imports | P1 | 完成|
| A15 | 第三方相依 | vcpkg manifest 為空；WinHTTP/DNS/ICMP 等系統庫 | `cppwinrt` + `onecore.lib` | 可選 GTK、curses、Jansson、libcap 等 | Hyper | 目前零第三方 runtime 適合 Windows 7；若加入 JSON library，選小型、可靜態連結且有 fuzz/安全維護者的套件 | P2 | 未開始 |
| A16 | feature detection | 以 Windows 版本與編譯條件為主，功能大多固定 | 以 SDK/WinRT 可用性為主 | configure 逐項偵測 IPv6、GTK、JSON、cap、curses、braille 等 | mtr | 為 Hyper 新增 runtime capability snapshot（IPv6 stack、ICMP handle、TLS、DPI API），錯誤訊息顯示缺失能力 | P2 | 未開始 |
| A17 | sanitizer 組態 | VS 有 ASAN debug 組態，但沒有 CI 執行 | 同樣有 ASAN 組態 | `--with-libasan` 可啟用 ASAN+UBSAN | mtr | Windows CI 每晚執行 x64 ASAN unit/integration tests；Release 前保留一輪 | P1 | 完成|
| A18 | compiler 嚴格度 | CMake `/permissive-`、`/utf-8`、`/Zc:__cplusplus`；VS 警告層級在組態間漂移 | 警告層級也不一致 | GCC/Clang + lint | 自訂 | 所有正式組態統一 `/W4 /WX`（第三方 header 例外）、`/sdl`、Spectre 選項按支援政策；CI 阻止下降 | P1 | 完成|
| A19 | source enumeration | CMake 使用 `GLOB CONFIGURE_DEPENDS` | 使用 `GLOB`，對 `.ixx` 的 CMake 支援較不完整 | Automake 明列 sources | mtr | CMake 明列 source/module file set；避免誤把暫存 `.cpp` 納入 build | P2 | 未開始 |
| A20 | 版本來源 | Branding header、CMake、manifest、resource 有多個同步點，另有文件說明 | 多處硬編碼 | configure/package version 集中並生成 man page | mtr | 建立單一 `version.json` 或 CMake version source，再生成 header/resource/manifest；CI 驗證一致 | P2 | 未開始 |
| A21 | 品牌與可見字串 | 品牌、URL、字型、來源集中於 `WinMTRBranding.h`；資源為繁中 | 品牌字串較分散 | gettext 型在地化並非此樹重點 | Hyper | 保留集中設計；將所有網路來源政策也改為 typed config，避免 macro/string 雙軌 | 保留/P2 | 部分完成 |
| A22 | 授權 | GPL-2.0 | GPL-2.0 | GPL-2.0，部分 BSD 授權程式 | 三者相容 | 可參考/移植 mtr GPLv2 程式，但複製 BSD 部分也要保留 BSD notices；發布附完整對應 source 與 notices | P1 | 完成|
| A23 | 發布平台文件 | 明列 Windows 7 SP1–11、x64、一般權限 | README 偏 Windows 10/11 | README 含 Unix、WSL、Cygwin | Hyper | 保留明確矩陣；每一支援 OS 必須有 CI/VM 或發布前驗證紀錄，否則降級為 best effort | P1 | 文件完成／Win7、Win10 VM 待外部驗證|
| A24 | 安裝／封裝 | 專案內仍有 Installer 組態但 README 主推 standalone | 有 Installer 組態 | `make install`、desktop/metainfo、Cygwin bundle | mtr | 建立可重現 ZIP/MSIX 或 installer pipeline、簽章、checksum、SBOM；不要依賴未被 CI 建置的舊 Installer 組態 | P2 | 未開始 |
| A25 | 原始碼規模與集中度 | UI display 1,261 行、trace 754 行、options 731 行，單檔偏大 | 功能少，檔案較小 | 模組較多但部分 C 檔仍很大 | 自訂 | 以責任邊界拆檔，不為行數拆檔；優先拆 trace scheduler、layout model、serializers、public-info providers | P1 | 部分完成（核心責任已拆；UI／serializer 可續拆）|

## 4. 探測 transport、排程、timeout、取消與回覆關聯

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| P01 | ICMP transport | `IcmpSendEcho2Ex`／`Icmp6SendEcho2` 同步呼叫 | 同 API 的 Event 非同步模式 | Unix raw/dgram socket；Cygwin 另有 Windows ICMP service thread | 依平台自訂 | Windows 保留 ICMP API adapter，不移植 Unix raw socket作為預設 | 保留 | 完成（既有） |
| P02 | 一個週期的排程模型 | 先建立本 round 全 TTL request；每 TTL 一個 `jthread` 同步等待，scope 結束 join 全批 | 長駐一個 coroutine/TTL，各 TTL 自己循環 | 單事件迴圈每 `interval/numhosts` 送下一 TTL；同時保留多個 outstanding probes | mtr | 重寫為連續 scheduler；週期只用於統計/配額，不作為 join barrier | **P0** | 完成 |
| P03 | TTL 彼此阻塞 | 同一 round 內平行，但下一 round 必須等最慢 TTL | 不同 TTL coroutine 不互相等候 | 完全不等候，reply/timeout 都是獨立事件 | mtr | 移除 round-wide join；任一 request 完成只更新該 hop | **P0** | 完成 |
| P04 | 同一 TTL 可否同時有多個未完成 probe | 不可；下一 round 要等上一 round全數完成 | 通常不可；每 TTL coroutine await 完成後才送下一包 | 可以；sequence/token 區分 outstanding probe | mtr | 每 TTL 允許 `ceil(timeout/interval)+安全餘量` 個 pending；設全域硬上限和 backpressure | **P0** | 完成 |
| P05 | interval 語意 | round 開始時間到下 round 開始時間；若 round 超過 interval，立即開始下一輪 | 有 reply 時 sleep `interval-RTT`；timeout 時沒有額外 sleep，因此語意依結果而變 | 每個 TTL 約每 `WaitTime` 收到一包，timeout 不改發送週期 | mtr | 定義為每 TTL 的 send-to-send cadence；使用 monotonic deadline，不以 RTT 補眠的分支語意 | **P0** | 完成 |
| P06 | timeout 語意 | 目前實際傳入 `min(configured timeout, interval)`；1 秒 interval 會把 3 秒 timeout 強制截成 1 秒 | API 參數填 5 秒，但 Windows async mode 不使用它；另以 threadpool wait 5 秒喚醒 | 每個 probe 10 秒 deadline，完全獨立於 1 秒週期 | mtr | 恢復 timeout 獨立設定；絕不可再和 interval 取最小值 | **P0** | 完成 |
| P07 | 現行預設 timeout | 1,000 ms（範圍 100–10,000） | 固定 5,000 ms | 10 秒 | 自訂 | GUI 預設建議 3,000 ms；進階/跨洲可調 10,000 ms。重點是排程獨立，而非哪一個預設值 | **P0** | 完成 |
| P08 | Windows async API 的 `Timeout` | 使用同步模式，API timeout 生效 | ⚠ 使用 Event 非同步模式；官方文件指出 async mode 不以 `Timeout` 完成 deadline | 不使用 Windows API | 自訂 | 應用層維護 deadline min-heap；詳見 [Microsoft IcmpSendEcho2](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmpsendecho2) 與 [Icmp6SendEcho2](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmp6sendecho2) | **P0** | 完成 |
| P09 | 非同步 buffer 生命週期 | 同步 call 返回後才釋放，安全但阻塞 worker | ⚠ 5 秒 timer 可 resume coroutine，但原始 ICMP request 未證明已完成；下一輪可能重用 request/reply buffer、event 或關閉 handle | 每個 outstanding probe heap allocation，收到 reply/timeout 後才 free | mtr/自訂 | 每包一個 stable `shared_ptr<ProbeRequest>`；request、event、reply buffer 直到 OS completion callback 後才回收，logical timeout 只改狀態、不釋放 | **P0** | 完成 |
| P10 | 回覆關聯 token | 每 TTL 一個獨立 handle/worker，藉 call 對應；沒有跨 round token | 每 TTL coroutine/固定 buffer，藉執行上下文對應 | token + ICMP id/sequence + outstanding list，最多 10,240 probes | mtr | 每包使用 64-bit session+sequence；completion 先驗證 session、generation、TTL、family，再更新 | **P0** | 完成 |
| P11 | late reply | 同步 timeout 返回後沒有該 call 的結果；沒有可接受 late reply 的模型 | timer 後的 OS late completion生命週期不清楚 | timeout 後 probe 被移除，late reply找不到 token即忽略 | mtr | logical timeout 先記 loss；OS 晚到 completion 只做資源回收且標記 `late_discarded`，不回改已完成統計 | **P0** | 完成 |
| P12 | xmit 計數時機 | `commitReply/commitTimeout` 才 `xmit++`，語意其實是 completed probes | await 返回後 `AddXmit`，同樣是 completed | send 時 `xmit++`；loss 分母排除仍在 transit 的 probes | mtr | 分開 `sent/completed/received/timed_out/in_flight`；UI Loss 只用 completed，Sent 顯示 sent 並可顯示 pending | **P0** | 完成 |
| P13 | 丟包判定 | API timeout或無 usable reply才 loss；部分 ICMP error視為 usable node | `dwReplyCount==0` 仍算 xmit；錯誤文字混入 host name | outstanding deadline到期才 loss；可傳遞 error result | mtr + Hyper | 保留 Hyper「ICMP error 是路徑事件」的方向，但另存 `ProbeOutcome/ErrorCode`，不要把錯誤文字當 hostname | P1 | 完成|
| P14 | ICMP status 分類 | `IP_REQ_TIMED_OUT`、`IP_GENERAL_FAILURE` 以外多數視為 usable；destination只認 `IP_SUCCESS` 且地址相同 | 只把 SUCCESS/TTL_EXPIRED 當 return，其餘寫文字 | 解碼 TTL-expired、echo reply、destination unreachable/no route 等 | mtr/自訂 | 建立明確 outcome table：reply、ttl-expired、destination-unreachable、packet-too-big、local-error、timeout；針對 v4/v6 測試 | P1 | 完成|
| P15 | destination 判定 | `IP_SUCCESS` 且 responder address 等於目標 | `GetMax()` 由 host address匹配目標或尾端重複地址推測 | protocol-aware：ICMP reply、UDP port unreachable、TCP/SCTP result；支援 due TTL | mtr | ICMP 沿用地址+status；未來 protocol adapter各自實作 `is_destination`，不可共用一條粗略規則 | P1 | 完成|
| P16 | 每 TTL handle | 依 max_hops 建一個 ICMP handle，最多 64 個 | 每 TTL一個 handle，共 30 個 | socket 依 protocol/family共用；stream probe另有 socket | 自訂 | 先量測 Windows ICMP handle 是否可安全承載並行 request；若可則每 family少量 handle pool，否則每 TTL handle但不每包重建 | P2 | 未開始 |
| P17 | thread 數 | 每 round 最多 max_hops 個 `jthread`，每秒反覆建立/銷毀 | coroutine 30 個，threadpool/WinRT 調度 | 少數 event-loop process/thread | mtr | 單 scheduler thread + Windows threadpool completion；避免每秒建立 30–64 OS threads | **P0/P1** | 完成 |
| P18 | CPU／喚醒 | stop/interval等待每 50 ms polling；每 TTL thread阻塞 API | WinRT timer/threadpool | `select()` 睡到最早 send/reply/timeout/DNS/UI deadline | mtr | 使用 waitable timer + completion event；沒有工作時零 polling | P1 | 完成|
| P19 | 時鐘 | `steady_clock` 排程；GetTickCount64 cache/time metadata | WinRT duration + event timer | `gettimeofday`/timeval，cache用 wall time | Hyper | 排程與 TTL cache一律 monotonic 64-bit；UTC只用輸出 timestamp | 保留 | 完成（既有） |
| P20 | cadence drift | 以 `cycle_started+interval` 可避免短 round累積 drift；長 round仍受 barrier | reply路徑以 `interval-RTT` 粗略維持 cadence；timeout路徑不同 | 事件迴圈依 last send slot；以 hop slot平攤 | 自訂 | 每 TTL保存 `next_due += interval`；落後時跳過過期 slot而非 burst 補發，記錄 scheduler lateness | P1 | 完成|
| P21 | 首輪發包方式 | 第一次 exploration 直接對 start..max_hops 全部同時送，最多 64 包 burst | 30 TTL coroutine幾乎同時啟動 | 逐 TTL spacing；只探到目標／unknown tail | mtr | 首輪逐 TTL stagger，預設一個 route cycle內均勻分布，避免瞬間 burst與 rate-limit | **P0/P1** | 完成 |
| P22 | 後續發包方式 | round ceiling內全 TTL 同時 burst | 每 TTL獨立 | `interval/numhosts` 均勻分布 | mtr | 在 interval內均勻排 TTL slot；可加 ±5% 可重現 jitter 避免同步效應，匯出實際 send time | P1 | 完成|
| P23 | 對無回覆 hop 的速率 | 被 synchronous timeout限制；目前因 timeout被縮成 interval而每秒一次 | 約每 5 秒一次，不是設定的 1 秒 | 仍每秒送新 probe，舊 probe各自等待10秒 | mtr | 無回覆 hop也維持使用者 interval；以 in-flight cap防止極端設定耗盡資源 | **P0** | 完成 |
| P24 | 對快速回覆 hop 的速率 | 每 round一次；受最慢 hop barrier | 約每 interval一次，較接近需求 | 每 interval一次 | mtr/Refresh意圖 | 目標為穩定 1 Hz（若 interval=1），不受其他 TTL timeout影響 | **P0** | 完成 |
| P25 | cycles 計數 | 全 round被接受後+1；reset epoch後重算 | 無 cycles設定，直到停止 | report `MaxPing` 以完成一批 TTL後+1 | 自訂 | cycles定義為「每個有效 TTL應排程的 probe 數」；結束後進 grace/drain，不讓路徑縮短造成不同 hop sample數未說明 | P1 | 完成|
| P26 | grace period | 停止時要求 stop，jthread同步 call自然 drain到實際（被縮短後的）timeout | stop令 coroutine退出，但 pending await生命週期不夠清楚 | 發完指定 cycles後等可設定 GraceTime，預設5秒 | mtr | 增加 `grace`：停止新發送，等待 min(remaining request deadline, grace)，UI顯示 pending數 | P1 | 完成|
| P27 | 使用者停止 | `request_stop`；無法取消已進入同步 ICMP call，最多等 timeout | atomic tracing=false；pending async wait仍須完成/喚醒 | 停發後 helper drain outstanding；SIGINT狀態明確 | mtr/自訂 | Stop立即停 scheduler；將 request標 cancellable/logically ignored，完成或 grace後安全關閉 transport | **P0/P1** | 完成 |
| P28 | 關閉程式 | EXIT state等 tracing/net inactive再關；另停止 network info | coroutine/WinRT context需收尾 | 關 command pipe後 helper drain probes再退出 | mtr/Hyper | 使用單一 structured-concurrency owner；禁止 detached task持有 UI指標，關閉有可觀察 phase與最長等待策略 | P1 | 完成|
| P29 | reset 統計 | `data_epoch`遞增，舊 worker commit會被拒絕；排程持續 | 直接清 host，缺少 session/epoch防舊結果 | net_reset清統計/sequence state | Hyper | 保留 epoch gate；新 scheduler每個 request帶 epoch，reset後晚到只回收不寫統計 | 保留 | 完成（既有） |
| P30 | session 防串資料 | `session_id` + `data_epoch` + snapshot revision | 沒有同等完整 generation模型 | process內 sequence/outstanding；每目標重開 net | Hyper | 保留並擴充為強型別 `SessionId/ProbeId/Epoch`，避免參數次序錯置 | 保留/P2 | 部分完成 |
| P31 | 失敗回復／可重啟 | guard會清 tracing；dialog捕捉例外 | coroutine exception路徑較複雜 | error多數直接退出 CLI | Hyper | GUI不能因單次 probe local error整個退出；transport fatal error才結束 session並顯示可行動資訊 | P1 | 完成|
| P32 | 記憶體上限 | probes每 round最多64；metadata cache 2,048 | 固定30 coroutine/buffer | outstanding hard cap10,240 | mtr | 設 `max_inflight = clamp(max_hops * ceil(timeout/interval)+margin, 128, 4096)`；達上限記 local-drop而非無限配置 | **P0** | 完成 |
| P33 | backpressure | 無需處理多 round pending，因 round barrier | 每 TTL單 pending | alloc失敗回錯、硬上限 | mtr | 超過全域或每 TTL上限時跳過該 send slot並增加 `scheduler_dropped`，不可算網路丟包 | P1 | 完成|
| P34 | payload固定模式 | 0–255；預設32；每 round共用 payload | 固定空白32 | 0–255；-1可隨 cycle random | 三者相同方向 | 保留；在 session snapshot記錄 pattern，random模式記seed以利重現 | P2 | 未開始 |
| P35 | payload隨機模式 | xorshift逐 byte，每 round新 payload | — | random bit pattern；也支援隨機 packet size | mtr + Hyper | 保留快速 PRNG，但用明確 PCG/xoshiro實作、session seed；加入 random packet size選項 | P2 | 未開始 |
| P36 | packet size範圍 | payload 0–4,096 bytes | 64–32,768 bytes | 完整 packet 28–65,535，負值代表隨機範圍 | 自訂 | UI區分「payload bytes」與「IP packet bytes」；安全上限依 v4/v6/API驗證，jumbo需清楚提示 fragmentation/MTU意義 | P2 | 未開始 |
| P37 | IPv4 DF | 預設 true，可設定 | 強制 `IP_FLAG_DF` | 依構造/socket平台行為，非同一 GUI選項 | Hyper | 保留；在 IPv6 UI說明 DF不適用，結果記 Packet Too Big/MTU | 保留 | 完成（既有） |
| P38 | ToS/DSCP | 0–255可設定 | — | 0–255可設定 | mtr/Hyper | 保留；UI最好同時顯示 DSCP(高6位)+ECN(低2位)，匯出 raw byte | P2 | 未開始 |
| P39 | source address／interface | — | — | `--interface`、`--address`，Linux `SO_BINDTODEVICE` | mtr | 增加可選 Windows adapter/source address；用 `IcmpSendEcho2Ex` SourceAddress（IPv4）及 IPv6 source，先檢查 route/family | P2 | 未開始 |
| P40 | fwmark／policy routing | — | — | Linux `--mark`/SO_MARK，並保留必要 capability | mtr | Windows沒有直接同義功能；不硬做等價 UI。可日後提供 interface/source/compartment，高級功能平台限定 | P3 | 未開始 |
| P41 | 目標／本機 port | — | — | TCP/UDP/SCTP target port；UDP local port | mtr | 加 TCP/UDP tracing時一併加入，ICMP模式禁用欄位並驗證互斥 | P2/P3 | 未開始 |
| P42 | transport capability negotiation | 只假設 ICMP API可用，create失敗成 local error | 同樣 | UI先向 helper查 IPv4/6、ICMP/UDP/TCP/SCTP支援 | mtr | 建立 `TransportCapabilities`；啟動前呈現可用協定與限制，錯誤不冒充packet loss | P2 | 未開始 |
| P43 | 原始封包 parsing | 無；由 Windows API解析 | 無 | 自己驗證 IPv4/IPv6、ICMP error queue、內嵌packet、MPLS | mtr（若需要） | ICMP API模式不新增解析面；只有 raw transport才獨立library並先配 fuzz corpus | P3 | 未開始 |
| P44 | local error與network loss分離 | `issued=false` 不 commit loss，方向正確；UI錯誤細節有限 | 部分錯誤文字放host欄，容易誤解 | helper傳 `permission-denied/invalid-argument/network-error` | mtr + Hyper | `ProbeOutcome`增加 `local_error`計數與最近錯誤；Loss只算真正已送出且deadline完成的 probe | P1 | 完成|
| P45 | 每包高解析 timestamp | RTT採 Windows API毫秒；completion tick為ms | 同為 API毫秒 | 內部微秒，輸出通常0.1ms | mtr | Windows scheduler使用 QPC記 send/completion與lateness；API RTT可保留比對，UI顯示0.1ms需確認底層精度 | P2 | 未開始 |

## 5. 路徑發現、目標解析、TTL、ECMP 與路由變動

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| R01 | 最大 TTL | 預設30，可設1–64 | 固定30 | 預設30，上限 `MaxHost-1`（255） | 自訂 | GUI保留64足以涵蓋一般用途；CLI/進階可提高255，但要先解除 UI、array與Windows wait限制 | P2 | 未開始 |
| R02 | first/start TTL | 可設1..max | 固定1 | `--first-ttl` | Hyper/mtr | 保留 | 保留 | 完成（既有） |
| R03 | minimum/due TTL | `minimum_ttl`使路徑 ceiling至少探到指定TTL；0停用 | — | `--due-ttl`要求到達指定TTL才可因目標/unknown停止 | mtr語意較清楚 | 將名稱改為「至少探測至 TTL」並定義到達條件；實作採 due-TTL，不只改 display ceiling | P1 | 完成|
| R04 | unknown host limit | 預設5，1–64 | 透過 `GetMax()`推測，無選項 | 預設12，可設定 | mtr | 預設建議10或12，避免連續5個silent hop過早截斷；UI說明是連續未知數 | P1 | 完成|
| R05 | 首輪路徑 ceiling | exploration先探到max_hops | 30個TTL全啟動，`GetMax`後個別退出 | 從first TTL向前，回覆出現時動態擴張，超過連續unknown才停止 | mtr | 採漸進式探索，避免每次首次目標直接burst 64包 | **P0/P1** | 完成 |
| R06 | destination到達後停止深探 | round後計算first destination，下輪縮短 | worker每次用`GetMax()`退出高TTL | batch發現destination即重啟，但已排程的較高TTL可能回覆 | mtr | 新scheduler停止安排destination之後的新probe；已in-flight晚到只記診斷事件，不增加正常路徑 | P1 | 完成|
| R07 | 無回覆目的端 | highest response + unknown tail | 尾端相同地址推測縮短，否則30 | maxUnknown後停止 | mtr | 使用最後有效回覆後連續unknown限額；「目的端不回ICMP」時保留可解釋的unknown range | P1 | 完成|
| R08 | 路徑縮短 | 每10 cycle exploration可用當輪取代歷史known，讓ceiling縮短 | `GetMax`依目前host array，地址一旦設定通常不清除到reset | `net_max`以當前host/target/error，歷史地址可能影響 | Hyper | 保留週期性重探，但改為非burst；加 hysteresis，連續2個探索確認後縮短避免抖動 | P1 | 完成|
| R09 | 路徑變長 | 沒有destination或目的端消失時 force exploration；每10 cycle也全探 | 高TTL worker若先前已退出不會重新建立，路徑變長偵測弱 | batch會依回覆/unknown持續動態調整 | mtr | 常態保留低頻 frontier probes（例如每10 cycle往後擴1–5 TTL），目的端消失立刻提高探索頻率 | P1 | 完成|
| R10 | route exploration週期 | 固定10 cycle constant | — | 每批動態，不需固定full sweep | mtr | 改為事件驅動+低頻frontier；將策略參數內部化並寫測試，不必暴露一般UI | P2 | 未開始 |
| R11 | 目標 DNS 解析 | 背景trace thread呼叫同步 `GetAddrInfoW` wrapper，不阻塞UI | WinRT async resolver | 啟動時同步 `getaddrinfo`; CLI可接受 | Hyper/Refresh | GUI保留背景解析；加入解析deadline、取消與錯誤分類 | P1 | 完成|
| R12 | IDN | Windows Unicode hostname交給 `GetAddrInfoW` | Unicode/WinRT | 有 `AI_IDN`時啟用 | Hyper | 保留；測試中文域名、punycode、尾點、IPv6 zone id與空白 | P2 | 未開始 |
| R13 | literal address | 先以 `WSAStringToAddressW`依允許family解析 | 支援 v4/v6 | `getaddrinfo` | Hyper | 保留快速路徑；IPv6 link-local zone id需明確保留scope | P1 | 完成|
| R14 | 多 A/AAAA候選 | 最多8；同時啟用時偏好IPv4，首輪逐一試可用性，再選第一個能reply/到達者 | 主要使用resolver選定地址 | `addrinfo`使用第一個結果；報告多目標非多地址fallback | Hyper | 保留候選fallback，但改為Happy-Eyeballs式平行/錯開可用性檢查，並顯示實際選定IP | P1 | 完成|
| R15 | IPv4/IPv6同時選項 | 兩者可同時，偏好IPv4並fallback IPv6 | 兩者可選，但追蹤單一resolved family | 一次session單一AF，`-4/-6` | Hyper | 保留，但UI文字應說「允許的位址族」而非同時追兩條；若要雙棧比較，新增兩個獨立session/view | P2 | 未開始 |
| R16 | 候選試跑成本 | 每候選做完整first round到max_hops；silent候選可能耗時/發大量包 | 單候選 | 單候選 | 自訂 | 可用性試跑先探TTL1、少數frontier與目標TTL不現實；較佳是兩候選各開短暫scheduler，第一個產生可用path即勝出並取消另一個 | P2 | 未開始 |
| R17 | 多個目標 | GUI一次一個；有history | GUI一次一個；有history | report模式可由檔案/argv依序多目標，要求相同AF | mtr | CLI增加 headless batch輸出時才支援多目標；GUI避免同窗混合統計 | P2 | 未開始 |
| R18 | host history | Registry保存、去重、上限1–256；session與persistent limit有處理 | Registry history，上限1–1024 | shell/history由外部處理 | Hyper | 保留；移除舊過大的限制差異或設512，並增加清除/隱私說明 | 保留/P3 | 部分完成 |
| R19 | hop主地址 | responders MRU首項成primary；路徑切換時主列會變成最近回覆 | 第一個有效地址設後不再更新，可能掩蓋路徑變化 | `addr`是最新回覆，另保留`addrs[]` | Hyper/mtr | primary可用「最近」但UI標示；更佳是依sample占比選dominant，recent responder另有指示 | P2 | 未開始 |
| R20 | ECMP多回覆保存 | 每TTL最多128；MRU排序，溢出直接pop最舊 | —，每TTL一地址 | 每TTL `MAX_PATH=128`，latest+address list | Hyper | 保留128與獨立display limit；改用stable ID + last_seen + hit_count，LRU淘汰而不是只按vector位置 | P1 | 完成|
| R21 | ECMP顯示上限 | 預設8，可設；完整匯出不受顯示上限 | — | 預設8，可設至128 | Hyper | 保留此「畫面限制≠資料限制」設計 | 保留 | 完成（既有） |
| R22 | ECMP responder metadata | 每responder有name/country/asn/isp/last seen | — | 可對多路徑做DNS/ipinfo/MPLS，但模型與輸出依mode不同 | Hyper | 保留；加來源、查詢時間、失敗狀態，避免空字串無法分辨未查/查無 | P2 | 未開始 |
| R23 | ECMP responder統計 | 所有回覆聚合到TTL主統計；替代列無loss/RTT | — | 同樣主要是hop聚合；TODO也提到per-host stats | **自訂** | 每個responder維護received、last/best/avg/worst/stddev；sent無法直接分配時顯示reply share，不能虛構per-responder loss | P2 | 未開始 |
| R24 | 路徑變更歷史 | 只保存目前responders與last seen | 只保存首次地址 | 保存多地址與400點RTT，但不是明確route-event log | **自訂** | 新增有上限的route events：時間、TTL、old/new dominant、首次/最後出現、family；可匯出JSON | P2 | 未開始 |
| R25 | unknown range壓縮 | UI把連續無回覆hop合併；snapshot/export仍逐hop | 每hop一列 | 每hop一列`???` | Hyper | 保留；合併列必須顯示TTL範圍與sent/loss語意，雙擊可展開 | 保留/P2 | 部分完成 |
| R26 | unknown range統計 | 合併列目前不顯示各hop統計，詳細資料以區間方式呈現 | 不適用 | 每hop可見 | 自訂 | 合併列顯示「每hop皆100%」只在所有completed均timeout時；若sample數不同則顯示範圍或展開，避免誤導 | P2 | 未開始 |
| R27 | 重複目的端回覆（超過真實TTL） | 第一個destination決定ceiling，較高結果不作主要路徑 | 尾端重複地址用來縮短 | raw mode可能報告已送出的更高TTL；curses過濾 | mtr | 主表只到第一個可靠destination；診斷event可保留post-destination reply但不混入正常hop | P2 | 未開始 |
| R28 | ICMP unreachable作終點 | 多數unreachable視usable，但只有success+same addr算destination | 顯示錯誤文字 | `net_max`遇host error可視final hop；protocol-aware | mtr | 針對unreachable code定義terminal/nonterminal；UI顯示原因，不能一律當一般router或一律當loss | P1 | 完成|
| R29 | NAT64 ASN | 一般IPv6 query；未特判Well-Known NAT64 | — | 對64:ff9b::取尾端IPv4做IPv4 ASN查詢 | mtr | metadata resolver加入RFC6052 well-known prefix；自訂NAT64 prefix需由系統DNS64偵測後才映射 | P2 | 未開始 |
| R30 | public/private判斷 | 明列v4私有、link-local、benchmark、test-net；v6只允許global 2000::/3並排除文件/ORCHID等 | 無外部metadata | ASN lookup可對各地址查，沒有Hyper同等HTTP隱私gate | Hyper | 保留；用prefix table+單元測試取代手寫if，更新特殊用途registry時可維護 | P1 | 完成|

## 6. 統計、樣本、歷史與量測語意

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| S01 | 計數型別 | `uint64_t` sent/received/total | `int`與`unsigned long`，長跑會溢位 | 多為`int`，長跑亦可能溢位 | Hyper | 保留64-bit；UI/Properties不得縮回int，匯出保留整數 | 保留/P1 | 完成|
| S02 | average | Welford `mean_ms` double；另有total | `total/returned`整數除法 | incremental integer usec average | Hyper | 保留Welford；移除或只為相容保留total，避免雙真相 | 保留 | 完成（既有） |
| S03 | best/worst/last | 有，毫秒整數 | 有，毫秒整數 | 有，內部微秒 | mtr精度 | transport若能取得更高精度改存`duration<double,milli>`或整數µs；UI再格式化 | P2 | 未開始 |
| S04 | loss percentage | double可輸出；UI四捨五入0位 | 整數percentage | 內部乘1000，可輸出2位 | mtr | UI預設1位或2位；低樣本時同時顯示sent，避免0/1包的百分比假精度 | P2 | 未開始 |
| S05 | in-flight是否算loss | 因xmit在完成時增加，不算，但Sent也看不到pending | 同樣await後才xmit | 明確從loss分母扣transit | mtr | 分拆sent/completed/inflight後，loss = timed_out/completed；UI tooltip給公式 | **P0** | 完成 |
| S06 | timeout後晚回覆 | 現同步模型沒有晚回覆 | 非同步模型可能混淆/覆寫 | timeout移除sequence後忽略 | mtr | late reply只計debug metric，不回溯改loss，確保統計單調可重現 | **P0** | 完成 |
| S07 | 標準差 | Welford M2，但除以N，為population stddev | — | 除以N-1，sample stddev | mtr | 欄名既是樣本觀測，建議N>1用sample stddev；N=1顯示0/空並在文件列公式 | P1 | 完成|
| S08 | current jitter | RFC1889風格 EWMA：`J += (abs(delta)-J)/16` | — | `jitter`是相鄰RTT絕對差（current） | 自訂 | 不要同欄混不同定義；保留Hyper EWMA但命名「平滑抖動」，另提供「最近差值」 | P1 | 完成|
| S09 | jitter avg/max | — | — | `Javg`、`Jmax` | mtr | 增加Welford/累積平均與max，使用64-bit/浮點，重設一致 | P2 | 未開始 |
| S10 | interarrival jitter | Hyper現有EWMA近似但以RTT差，未命名公式來源 | — | `Jint`依RFC1889式1/16濾波 | mtr | 明確採RFC3550/1889公式並文件化輸入是RTT variation而非單向transit；避免錯稱網路標準原義 | P2 | 未開始 |
| S11 | geometric mean | — | — | 有Gmean | mtr | 可加進階欄位，但不是主表預設；用log-domain避免overflow/underflow | P3 | 未開始 |
| S12 | dropped packets欄 | loss可由sent-returned推算，沒有獨立欄 | 同 | 可選Drop欄 | mtr | 詳細資料/匯出增加`timed_out`與`local_dropped`；主表不一定新增欄 | P2 | 未開始 |
| S13 | RTT歷史 | 只保留聚合統計 | 只保留聚合 | 每hop保存最近400 ping，供文字／block／braille graph | mtr | 每hop固定ring buffer，元素含probe id、send time、RTT/outcome/responder；預設400或依分鐘換算 | P2 | 未開始 |
| S14 | latency graph | — | — | curses文字、block map、braille圖 | mtr | GUI新增可選迷你sparkline/heatmap，不佔主表預設寬度；資料來自ring buffer | P3 | 未開始 |
| S15 | percentile | — | — | —（TODO提到） | **自訂** | 以ring buffer計P50/P95/P99；明確標示視窗長度，不把全程與最近視窗混合 | P3 | 未開始 |
| S16 | sliding-window loss | —，全session累積 | — | —，全session累積；TODO提到circular loss | **自訂** | 同時保留Lifetime與最近N包/分鐘；GUI預設Lifetime，詳細資料可切換 | P3 | 未開始 |
| S17 | sample reset | Reset遞增epoch並全清；metadata cache保留 | 全清host | `r`/net_reset清計數與saved | Hyper | 保留metadata與DNS cache、只清measurement；UI明示開始時間重設 | 保留 | 完成（既有） |
| S18 | reply cache對樣本的影響 | cache命中時跳過送包且不增加sent；等同暫停該hop樣本 | — | `--cache`跳過recently seen hop；同樣降低樣本 | 自訂 | 預設關閉正確；啟用時UI顯示Cached/Skipped，不可讓使用者誤以為每秒仍量測 | P1 | 完成|
| S19 | 不同hop樣本數 | round模型通常相近，但cache/ceiling/候選試跑造成差異 | TTL早退後樣本不同 | 動態path與cache會不同 | 自訂 | 每列永遠顯示sent/completed；匯出加入active_from/to與skipped count | P2 | 未開始 |
| S20 | responder share | 只有last_seen，無次數比例 | — | address list未直接輸出比例 | **自訂** | 每responder累加reply_count，顯示占該TTL replies百分比，幫助判斷ECMP比例 | P2 | 未開始 |
| S21 | 統計snapshot一致性 | mutex下複製完整snapshot，revision/epoch一致 | 多getter/recursive mutex，整體一致性較弱 | event loop單thread更新，UI讀全域 | Hyper | 保留immutable snapshot；metadata與probe完成合併為transaction，revision只增一次 | 保留/P2 | 部分完成 |
| S22 | route與統計生命週期 | 每session全清 | 每trace全清 | 每目標net_open/reset | 三者一致 | 增加可選「比較前一次」應另存completed snapshot，不要偷偷延續統計 | P3 | 未開始 |
| S23 | 開始時間／持續時間 | UI/JSON沒有完整session timestamps | 無 | report輸出Start timestamp | mtr | snapshot加入`started_at_utc`、`duration`、`ended_at`；所有檔案格式輸出 | P1 | 完成|
| S24 | 統計公式版本 | 無schema/formula version | 無 | 格式歷史成熟但公式沒有machine version | **自訂** | JSON加入`schema_version`與`statistics_definition`；修改公式時升版 | P1 | 完成|

## 7. DNS、ASN/ISP、公網資訊、快取、來源政策與隱私

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| N01 | 反向DNS | `GetNameInfoW`背景 detached thread | WinRT背景 coroutine `GetNameInfoW` | 獨立resolver process，pipe回傳 | mtr概念/Hyper API | Windows使用有上限threadpool resolver queue；不要每地址開 detached thread | P1 | 完成|
| N02 | DNS是否阻塞probe | 不阻塞；reply commit後排背景工作 | 不阻塞 | resolver fd整合event loop | 三者都正確 | 保留非阻塞 | 保留 | 完成（既有） |
| N03 | DNS request去重 | `session:epoch:address` inflight set | 每hop首次地址觸發，跨hop去重能力有限 | resolver result list按IP cache | Hyper/mtr | inflight key應只用address+query type，跨session共享結果但consumer以session gate更新 | P2 | 未開始 |
| N04 | 反向DNS快取 | responder metadata cache 24h、2,048 entries，包含已查但空結果 | 沒有明確TTL/容量共享cache | process內結果cache，沒有明確TTL | Hyper | 保留bounded TTL；正/負cache使用不同TTL（如24h/10min），尊重DNS PTR TTL若可取得 | P1 | 完成|
| N05 | cache淘汰 | 容量滿時線性找`cached_at_tick`最舊項，近似FIFO age | — | `hsearch`/linked result，session結束釋放 | 自訂 | 2,048很小仍可接受；重構時用LRU list+unordered_map，更新命中時間並暴露metrics | P3 | 未開始 |
| N06 | metadata查詢範圍 | 公網hop才送外部HTTP/DNS；私網只PTR | 無ASN/ISP | DNS-based ipinfo可查address | Hyper | 保留public-address gate，避免把私網位址洩漏給外部服務 | 保留 | 完成（既有） |
| N07 | hop ASN/ISP主要來源 | ipinfo v4/v6 per-IP endpoint | — | Team Cymru型DNS provider，可自訂v4/v6 provider | mtr的低洩漏/可設定性 | 對大量hop預設採Team Cymru DNS ASN；HTTP地理/ISP改為可選且限速。避免每hop多次HTTPS造成慢與隱私成本 | P1 | 完成|
| N08 | hop metadata備援 | ipinfo完全無metadata才ipapi，再Cymru；不混合來源 | — | 單一設定provider | Hyper政策 | 保留「primary完全不可用才fallback、不拼欄位」；結果記source與failure reason | 保留/P1 | 完成|
| N09 | 公網IPv4資訊 | ipinfo.io；完全失敗才ipapi.co，再ipify只取IP | — | — | Hyper | 保留；provider interface與policy分離，測試不得直接打production服務 | P1 | 完成|
| N10 | 公網IPv6資訊 | v6.ipinfo.io；同類fallback | — | — | Hyper | 保留；必須強制request經IPv6路徑，測試只有IPv4時的快速失敗 | P1 | 完成|
| N11 | 遞迴DNS/ECS資訊 | `whoami.ds.akahelp.net` TXT；顯示resolver公網IP、ECS subnet與支援狀態 | — | — | Hyper | 保留；文件註明結果代表實際遞迴resolver路徑，不一定是Windows設定的DNS server | 保留 | 完成（既有） |
| N12 | 本機DNS server | `GetAdaptersAddresses`列出up adapters的DNS server | — | 不在主功能中列 | Hyper | 保留為環境資訊，且不列入外部「資料來源」；加adapter名稱/metric可選詳細欄 | 保留/P2 | 部分完成 |
| N13 | 公網資訊來源混用 | 每個IPv4/IPv6 result不混欄位；DNS metadata本身可用其獨立單一provider | — | ipinfo單provider | Hyper | 保留；UI每section顯示該section source，比底部總清單更可稽核 | P1 | 完成|
| N14 | Windows網路設定列入來源 | 本機DNS有顯示，但不加入successfulSources | — | — | Hyper | 現行符合需求，保留 | 保留 | 完成（既有） |
| N15 | refresh觸發 | 啟動；網路介面變更+5秒debounce，或固定1–1,440分鐘 | — | ASN/DNS隨路徑需求 | Hyper | 保留；另加手動Refresh與last-updated，失敗採exponential backoff | P1 | 完成|
| N16 | refresh時舊資料 | 查詢中/完成狀態存在，但應確認UI是保留舊值還是先清 | — | 不適用 | 自訂 | refresh期間保留上次成功值並顯示「更新中」；成功原子替換，失敗顯示stale與錯誤，不把舊值當新值 | P1 | 完成|
| N17 | 網路變更通知 | `NotifyIpInterfaceChange`，debounce後刷新 | — | route由持續probe自然反映 | Hyper | 保留；關閉時先取消notification，callback只PostMessage不碰UI物件 | 保留/P1 | 完成|
| N18 | 外部HTTP並行／順序 | queryCurrent依IPv4、IPv6、WhoAmI、DNS metadata序列執行；hop每地址開thread | — | ASN透過DNS同步lookup/cache | 自訂 | 公網v4/v6/akahelp可受控平行；hop metadata用固定concurrency queue（如4），per-host rate limit | P1 | 完成|
| N19 | HTTP timeout | WinHTTP connect/send 3s、receive約3.5s；stop token只能在阻塞call前後檢查 | — | DNS query由resolver控制 | 自訂 | WinHTTP async callback或可關閉request的owner；全query budget如5–8秒，stop立即關handle中斷 | P1 | 完成|
| N20 | response大小 | 上限1 MiB | — | DNS response固定小 | Hyper | 保留，但JSON服務正常應限制64KiB；超限記provider malformed | P2 | 未開始 |
| N21 | JSON parsing | 手寫只取string key；Unicode `\u`只處理單一16-bit值，沒有完整surrogate/object語法驗證 | — | JSON輸出用Jansson，不解析HTTP JSON | mtr使用成熟library的原則 | 採小型、fuzz過的JSON parser；至少測escaped key、surrogate pair、duplicate key、null、超深／超長輸入 | P1 | 完成|
| N22 | UTF-8驗證 | `MultiByteToWideChar(...MB_ERR_INVALID_CHARS)`，方向正確 | WinRT string處理 | locale/C strings | Hyper | 保留嚴格UTF-8；provider malformed不得部分顯示亂碼 | 保留 | 完成（既有） |
| N23 | country在地化 | country code經Windows Geo DB強制zh-TW friendly name | — | 顯示provider的country code | Hyper | 保留；若Geo DB沒有資料就顯示ISO code，別改用另一provider補欄位 | 保留 | 完成（既有） |
| N24 | 組織字串解析 | 解析`ASnnn Org`成ASN+ISP | — | Cymru TXT欄位直接拆 | 三者皆簡單 | 建立strict parser與測試（AS-SET、空格、多ASN、超長）；保存raw organization供debug | P2 | 未開始 |
| N25 | Team Cymru解析 | origin TXT取ASN，再AS name TXT取最後欄 | — | origin provider可自訂，欄位含ASN/route/country/registry/date | mtr | 擴充Hyper resolver result為ASN、prefix、country、registry、allocated可選；ISP名稱查詢有獨立TTL | P2 | 未開始 |
| N26 | provider可設定 | Branding header編譯期固定 | — | CLI可自訂IPv4/IPv6 ipinfo DNS suffix與fields | mtr | 一般UI只列受信任內建provider；進階/企業版可設定DNS provider，HTTPS URL不要無驗證任意接受 | P2 | 未開始 |
| N27 | provider健康狀態 | 只有空結果/fallback，沒有cooldown | — | DNS失敗每新key仍可能查 | **自訂** | circuit breaker：連續失敗後provider cooldown；顯示暫停原因，避免大量hop放大故障 | P1 | 完成|
| N28 | retry | 每次query按provider鏈重試，沒有指數backoff/jitter | — | resolver行為依系統 | **自訂** | 一次刷新每provider最多一次；跨刷新exponential backoff+隨機jitter；手動刷新可override一次 | P1 | 完成|
| N29 | metadata negative cache | `hostname_queried/network_queried`即使空也cache 24h | — | DNS cache空結果行為較弱 | 自訂 | negative TTL 5–15分鐘，不用24h；NXDOMAIN按DNS TTL，暫時網路錯誤不negative-cache太久 | P1 | 完成|
| N30 | reply cache | 可設0–86,400秒，預設0；只要該TTL最近有reply就跳probe | — | `--cache SECONDS`，預設功能關但duration預置60 | mtr/Hyper | 保留預設關；cache key應含target、family、TTL、transport、size/TOS/source，路徑事件後invalidate | P1 | 完成|
| N31 | reply cache destination | 另記last_destination_reply_tick | — | hop `up/seen` | Hyper | 保留；route/interface/target/options變更立即清 | 保留/P1 | 完成|
| N32 | reply cache與ECMP | cache命中只代表TTL最近有任一responder，會降低看到ECMP變化機率 | — | 同樣可能隱藏path change | 自訂 | 啟用cache時仍以低頻probe刷新，或明示是低流量監控模式；不要用於即時診斷預設 | P2 | 未開始 |
| N33 | 公網資訊cache | 以last query與設定觸發，但沒有跨啟動持久cache | — | — | 自訂 | 記憶體內保留last-success即可；跨啟動不建議持久保存公網IP，避免隱私與stale | 保留 | 完成（既有） |
| N34 | proxy | WinHTTP `DEFAULT_PROXY`，沿用系統設定 | — | DNS/Unix network | Hyper | 保留；詳細錯誤要區分proxy auth/TLS/DNS/timeout，但不可記錄credential | P2 | 未開始 |
| N35 | TLS政策 | 顯式允許TLS1.0/1.1/1.2以顧Windows7，沒有TLS1.3 | — | provider DNS無TLS | 自訂 | Windows10+用OS secure defaults；Windows7最低TLS1.2，若不可用就清楚失敗，不主動降TLS1.0/1.1 | P1 | 完成|
| N36 | certificate驗證 | 使用WinHTTP預設驗證，未關閉錯誤 | — | DNS無DNSSEC驗證控制 | Hyper | 保留系統驗證；不加入忽略憑證開關；錯誤可顯示但不可洩漏敏感內容 | 保留 | 完成（既有） |
| N37 | User-Agent | 集中品牌常數 | — | 系統resolver | Hyper | 保留，加入版本但避免唯一裝置識別碼 | 保留 | 完成（既有） |
| N38 | 資料來源呈現 | 詳細資料底部列本次成功來源 | — | provider由CLI/輸出欄位隱含 | Hyper | 保留並加每section source/queried-at；fallback reason只放診斷log | P2 | 未開始 |
| N39 | 使用者同意／關閉 | 選項可關閉公網資訊查詢 | — | ASN查詢需參數啟用 | mtr的opt-in隱私較保守 | 首次啟動簡短告知會連線哪些服務；維持一鍵關閉，關閉後不做任何外部metadata HTTP | P1 | 完成|
| N40 | DNS與ASN開關分離 | resolve hostname與lookup ASN/ISP可分開 | 只有DNS | DNS、AS/ipinfo分開 | Hyper/mtr | 保留；再分「DNS-based ASN」與「HTTPS地理/ISP」，讓隱私選擇精確 | P2 | 未開始 |
| N41 | log中的敏感資料 | 幾乎沒有持久log | 無 | CLI輸出即資料 | 自訂 | 若新增diagnostic log，預設不記完整目標/公網IP；提供一次性可匿名化support bundle | P2 | 未開始 |

## 8. UI、版面、更新頻率、互動、可及性與設定

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| U01 | 介面語言 | 完整繁體中文（台灣），技術欄保留適當英文 | 主要英文 | CLI英文 | Hyper | 保留；所有新功能同步resource、tooltip、README | 保留 | 完成（既有） |
| U02 | 字型 | UI與技術資料字型集中；表格/hostname等用技術字型 | 系統/MFC既有設定 | terminal/GTK字型由環境 | Hyper | 保留；不要用固定字元數推欄寬，永遠以實際font+DPI measure | 保留 | 完成（既有） |
| U03 | 主表欄位 | 14欄：Host、Hop、Loss、Sent、Received、Best、Avg、Worst、Last、Jitter、StdDev、Country、ASN、ISP | 9欄，無jitter/metadata | 動態可選15種統計+ipinfo | mtr的可配置性 | 保留14欄預設；新增欄選擇/排序/重設，儲存使用者配置 | P2 | 未開始 |
| U04 | 欄對齊 | 數值欄右對齊，文字左對齊 | 基本對齊 | format width固定 | Hyper | 保留；loss與單位一致，header tooltip列公式 | 保留 | 完成（既有） |
| U05 | 欄寬 | 每次revision自動量測data/header；ISP吃剩餘空間 | 固定/較簡單 | report-wide可依最長hostname，TUI依terminal | Hyper | 資料新增時可增寬，正常更新不要每包縮放造成視窗跳動；提供「最佳寬度」命令 | P1 | 完成|
| U06 | DPI變更 | `WM_DPICHANGED`重建字型、重設render revision再量測欄位 | 缺少同級完整處理 | GTK/terminal由環境 | Hyper | 現行強制重算方向正確；加monitor切換測試 | 保留/P1 | 程式完成／多螢幕 DPI 待外部驗證|
| U07 | 啟動視窗大小 | 依公網資訊自然寬度與最小內容高度，自動限制於work area | 固定dialog | GTK/terminal自行 | Hyper | 保留；第一次network data回來只擴一次，避免反覆搶使用者手動尺寸 | P1 | 完成|
| U08 | 追蹤後視窗大小 | 依欄位與所有display rows調整；路徑縮短可縮 | 固定/基本resize | terminal不適用 | Hyper | 只有`firstDataResize`或使用者選「自動調整」時改外窗；追蹤中後續只調內部layout，避免干擾 | P1 | 完成|
| U09 | 垂直scrollbar | 內容可容納螢幕時擴高到完整列；超出才scroll | 固定list可scroll | terminal自然scroll | Hyper | 保留；計算必須含header、horizontal scrollbar、statusbar與frame | 保留 | 完成（既有） |
| U10 | 水平scrollbar | 欄總寬超過螢幕才出現 | list可scroll | wide report/terminal截斷 | Hyper | 保留；不要強行把ISP欄縮到不可讀 | 保留 | 完成（既有） |
| U11 | layout repaint | `SetRedraw(FALSE)`、defer move、double-buffer list，完成後一次invalidate | MFC基本重繪，歷史上較易重影 | curses redraw有節流 | Hyper | 保留；所有WM_SIZE/DPICHANGED path共用同一layout transaction | 保留 | 完成（既有） |
| U12 | 背景色一致 | 以`COLOR_3DFACE`處理static/group/status；停用group theme以避免白底 | 系統theme預設 | terminal/GTK theme | Hyper | 系統高對比模式時不要強制classic group theme；偵測高對比並回到系統繪製 | P1 | 完成|
| U13 | 每包UI更新 | worker完成每TTL後post `messageTraceDataChanged`；另有500ms fallback timer | 100ms timer但每10次才redraw，約1秒polling | reply即處理，terminal repaint另節流 | Hyper/mtr | 新scheduler每個completion更新model；UI以coalesced 50–100ms frame呈現，既即時又避免每秒數十次重建整表 | **P0/P1** | 完成 |
| U14 | 現行首輪逐hop顯示 | snapshot只公開從start TTL起連續completed prefix，避免高TTL先完成造成假unknown | worker獨立但UI約1秒一次 | 動態逐TTL顯示 | Hyper | 新scheduler不必等prefix才顯示；未完成列可顯示「等待中」，不能當loss/unknown | P1 | 完成|
| U15 | redraw成本 | 每revision刪除所有items、重建所有列、重新量14欄 | 重填list | curses重繪；event loop節流 | 自訂 | keyed row diff更新；只有文字改變的cell SetItemText，新增/刪除route時才重建；欄寬低頻debounce | P1 | 完成|
| U16 | selection保持 | 依row kind/TTL/responder address重選 | list selection較容易因重畫丟失 | TUI cursor | Hyper | 保留stable row key；新增每responder ID避免MRU reorder使selection跳動 | 保留/P2 | 部分完成 |
| U17 | target開始後反藍 | 已清selection並hide combo edit selection，disable後不反藍 | 可能保留selection | CLI不適用 | Hyper | 保留並加keyboard focus測試 | 保留 | 完成（既有） |
| U18 | Start/Stop state machine | IDLE/TRACING/STOPPING/EXIT；Stop顯示等待packets | 類似state machine，但async生命週期較弱 | signal/grace/loop退出 | Hyper + mtr | 保留UI state；底層回報`inflight`與deadline，STOPPING顯示實際剩餘數 | P1 | 完成|
| U19 | Pause | — | — | curses可pause發送並繼續UI | mtr | 增加「暫停」：停止新send但保留session、處理pending；Resume按原cadence重排，不burst補發 | P2 | 未開始 |
| U20 | Reset | 可追蹤中reset，epoch防舊結果 | 可reset | curses `r` | Hyper | 保留；reset後在途舊包不算新epoch | 保留 | 完成（既有） |
| U21 | 節點詳細資料 | 主節點、替代responder、unknown range皆有dialog；含統計/metadata | 只在tracing且基本欄位 | TUI/report直接展開 | Hyper | 保留；允許停止後開啟；新增source/timestamps/responder share/error outcomes | P2 | 未開始 |
| U22 | 公網詳細資料 | 獨立可resize dialog、tab對齊文字、複製 | — | — | Hyper | 改為兩欄ListView或read-only grid，section header可展開；保留複製成純文字 | P2 | 未開始 |
| U23 | 公網摘要配置 | 上四下二布局，依實際文字分配間距並在空間不足才ellipsis | — | — | Hyper | 保留；hostname/ISP tooltip顯示完整值，keyboard focus可讀 | 保留/P2 | 部分完成 |
| U24 | hostname/ISP截斷 | 摘要空間不足才ellipsis；詳細資料完整 | — | report可wide | Hyper/mtr | 保留；主表同樣提供完整tooltip與copy cell | P2 | 未開始 |
| U25 | ECMP互動 | 替代responder獨立列、雙擊詳細資料 | — | 額外行，TUI顯示限制 | Hyper | 增加TTL列expand/collapse，預設收合大量ECMP並顯示`+N` | P2 | 未開始 |
| U26 | unknown range互動 | 合併顯示、雙擊區間詳細 | 每hop | 每hop | Hyper | 加展開按鈕/鍵盤操作，避免只能雙擊 | P2 | 未開始 |
| U27 | latency history視覺化 | — | — | curses blockmap、braille、strip chart scales | mtr | 節點詳細dialog先加sparkline；全表heatmap列為P3 | P3 | 未開始 |
| U28 | 動態scale | — | — | 自動或`fast/average/slow/custom` thresholds | mtr | graph加入auto percentile scale與固定preset，勿改變主統計 | P3 | 未開始 |
| U29 | compact mode | 自動內容大小但非column compact preset | — | curses compact toggle | mtr | 小螢幕提供「精簡欄位」preset，保留完整匯出 | P3 | 未開始 |
| U30 | DNS/IP顯示切換 | DNS選項需到Options；主表顯示name或IP | 類似 | `n` toggle、`show-ips`可同時顯示 | mtr | 主表context menu提供「主機名稱／IP／兩者」即時切換，不重新探測 | P2 | 未開始 |
| U31 | 欄位順序 | 固定14欄 | 固定9欄 | `--order`及TUI互動可選 | mtr | 可拖曳header並持久化；匯出用stable schema，不必跟UI順序綁死 | P2 | 未開始 |
| U32 | 分享樣本提醒 | 第一個實際TTL未達100包時提醒 | — | report cycles由使用者決定 | Hyper | 保留，但提供「仍然匯出」；門檻不應阻止自動化CLI | 保留/P2 | 部分完成 |
| U33 | screenshot | 完整視窗截圖到clipboard | — | — | Hyper | 保留；高DPI用PrintWindow/DWM fallback測試，失敗不留半個clipboard transaction | 保留/P2 | 部分完成 |
| U34 | statusbar hyperlink | 自訂company link作pane唯一內容，清空pane文字避免重疊 | 原實作有indicator/link重疊風險 | — | Hyper | 保留現修法；支援keyboard、focus cue與accessible name | 保留/P2 | 部分完成 |
| U35 | 鍵盤操作 | MFC標準Tab/按鈕；部分功能靠雙擊/menu | MFC標準 | curses有完整快捷鍵/help | mtr | 為list context/expand/details/copy提供Enter、Space、Ctrl+C與menu access keys | P2 | 未開始 |
| U36 | screen reader | 未見自訂UIA語意；原生controls有基本支援 | 同 | terminal text天然可讀但圖形模式有限 | 自訂 | 為custom hyperlink、合併unknown、ECMP列設accessible name/role/state；自動更新用polite live region策略 | P2 | 未開始 |
| U37 | 高對比／暗色 | 多數用system colors，但classic group/theme自訂需驗證；無完整dark mode | 系統MFC | terminal/GTK theme | 自訂 | 高對比為P1；dark mode可P3。不得硬編白/黑，截圖/export HTML另行定義 | P1/P3 | 程式完成／高對比截圖待外部驗證|
| U38 | RTL/其他語系 | 專注zh-TW | 英文 | 多locale terminal | 非目標 | 不需要為未承諾語系擴張；但resource ID與layout保持可在地化 | P3 | 未開始 |
| U39 | Options內容 | interval、size、max hops、timeout、cycles、TOS、pattern、history、start/minTTL、unknown、ECMP limit、reply cache、DNS、ASN、DF、v4/v6、公網與refresh | interval、size、history、DNS、v4/v6 | CLI選項最廣 | Hyper + mtr | 保留GUI；以Basic/Advanced分組，避免長頁只靠scroll | P2 | 未開始 |
| U40 | Options小螢幕 | 有水平/垂直scroll與responsive layout | 固定dialog | CLI | Hyper | 保留，但基本頁不應需要水平scroll；技術值用spin/edit+單位suffix | P2 | 未開始 |
| U41 | Options驗證 | 集中常數、解析後range驗證；v4/v6至少一個 | MFC DDV，invalid CLI值退default且警示不足 | CLI錯誤即非零退出並說明 | mtr | GUI inline error聚焦欄位；CLI invalid一定非零/不開始，不能悄悄改default | P1 | 完成|
| U42 | Restore defaults | 有 | 無同級完整 | CLI不適用 | Hyper | 保留；只改dialog draft，按OK才持久化 | 保留 | 完成（既有） |
| U43 | 設定持久化 | Registry，range clamp，command-line override不直接寫回 | Registry | `MTR_OPTIONS`環境與CLI，不持久化UI | Hyper | 增加settings schema version與migration；壞值記診斷後回default | P1 | 完成|
| U44 | 清除歷程 | 有獨立動作 | 有 | — | Hyper | 保留；清除後不可因session copy在下一次又寫回 | 保留 | 完成（既有） |
| U45 | auto-start CLI target | 有target即post Start | 有 | CLI直接開始 | 三者 | 保留；錯誤exit code需對automation可見 | P1 | 完成|

## 9. 命令列、自動化與設定表面

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| C01 | CLI廣度 | 只有target、interval、size、history、numeric、help | 同一小組 | 40+選項，涵蓋transport/path/output/UI | mtr | GUI選項都應有等價long CLI；短選項只保留無衝突常用項 | P1 | 完成|
| C02 | CLI錯誤 | 已能報unknown/missing/multiple target/invalid並顯示中文 | unknown可能直接忽略；invalid回default | 明確stderr+failure exit | mtr | 增加真正process exit code；GUI message可有，但automation不能只靠dialog | P1 | 完成|
| C03 | Unicode/quotes | 使用Windows Unicode tokenization，支援空白/非ASCII | 同 | shell argv/AI_IDN | Hyper | 保留；README列Windows escaping範例 | 保留 | 完成（既有） |
| C04 | `--help` | 開GUI help/dialog | 類似 | stdout完整usage | mtr | `--help`/`--version`在console可用時輸出stdout並0退出；GUI啟動另可開help | P2 | 未開始 |
| C05 | headless report | — | — | 核心能力 | mtr | 新增`--report --format json/csv/text`，使用同一scheduler/model/serializer但不建MFC dialog | P2 | 未開始 |
| C06 | 指定cycles | GUI有但CLI沒有 | — | `--report-cycles` | mtr | 加`--cycles`，0表示持續；headless必須要求finite或明確Ctrl+C | P1 | 完成|
| C07 | interval subsecond | 0.1–60秒，一般使用者可選 | 最小1秒 | 非root不得<1秒，root可更低 | Hyper（Windows API） | 允許0.1但加rate提示與全域pps cap；小於1秒不是提高權限理由 | P2 | 未開始 |
| C08 | multiple target file | — | — | `--filename` | mtr | 只在headless batch加入；逐目標清楚分隔/個別exit status | P3 | 未開始 |
| C09 | per-target port syntax | — | — | `hostname:port` + protocol | mtr | 新增TCP/UDP transport後採顯式`--port`，IPv6 bracket syntax需測試 | P3 | 未開始 |
| C10 | 環境預設 | — | — | `MTR_OPTIONS`先解析 | mtr但有風險 | Windows可支援`WINMTR_OPTIONS`，但registry/UI precedence必須文件化：built-in < registry < env < CLI | P3 | 未開始 |
| C11 | 欄位選擇 | — | — | `--order` | mtr | headless使用stable field names而非單字母；GUI profile可映射 | P2 | 未開始 |
| C12 | provider選擇 | 編譯期 | — | `--ipinfo_provider4/6` | mtr | 僅進階CLI允許經驗證DNS suffix；HTTPS URL需allowlist或明確unsafe標記 | P3 | 未開始 |
| C13 | exit status | GUI導向，未建立細緻headless status | 同 | interactive/report統一success/failure處理 | mtr | 定義0成功、2參數、3解析、4transport、5無任何reply、130取消；文件化 | P2 | 未開始 |
| C14 | bash/shell completion | — | — | bash completion | mtr | 若headless CLI擴充，提供PowerShell completion與winget manifest；bash非Windows主需求 | P3 | 未開始 |
| C15 | 設定列印 | — | — | `--version`可列build features | mtr | `--diagnostics`輸出版本、commit、OS、DPI、transport capabilities、有效options（隱私欄遮罩） | P2 | 未開始 |

## 10. 複製、匯出、格式互通與資料保存

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| O01 | 純文字複製 | 14欄tab-separated，含target與所有ECMP responder | 9欄文字 | report/raw/split等stdout | Hyper | 保留；加session時間/options/source address摘要 | P2 | 未開始 |
| O02 | HTML clipboard | 同時放CF_UNICODETEXT與標準HTML Format fragment，會escape HTML | 有基本HTML copy | — | Hyper | 保留；加入`.responder`視覺樣式與可讀caption | 保留/P2 | 部分完成 |
| O03 | TXT檔 | UTF-8，含BOM | 有 | report redirect | Hyper | 保留；寫入temp後`ReplaceFile/MoveFileEx`原子替換，避免中途失敗留下半檔 | P1 | 完成|
| O04 | HTML檔 | 完整zh-Hant-TW文件、CSS、escape | 有基本HTML | — | Hyper | 保留；加入metadata table與generator/schema version | P2 | 未開始 |
| O05 | CSV | 14欄、quote comma/quote/newline，UTF-8 | — | CSV含version/time/status/target/hop/IP/fields，但字串escaping較弱 | Hyper escaping + mtr metadata | CSV每列加入target/session/hop/responder kind/IP；防試算表公式注入（`= + - @`）並文件化 | P1 | 完成|
| O06 | JSON | 手寫object；含target、hop、host/IP、統計、country/asn/isp、完整responders | — | Jansson可選；含src/dst/tos/tests/psize/pattern/hubs/動態fields | mtr schema內容 + Hyper ECMP | 定義versioned JSON schema，加入app/commit/options/timestamps/outcomes/route events；用正式JSON writer | P1 | 完成|
| O07 | XML | — | — | 完整XML報告 | mtr | 若有企業/舊系統需求再加；優先維持JSON/CSV，不為格式數量而增加維護面 | P3 | 未開始 |
| O08 | raw event stream | — | — | host/xmit/ping/DNS/MPLS events | mtr | headless新增NDJSON event mode最實用；每行schema-versioned event，比複製mtr legacy raw格式更適合現代consumer | P2 | 未開始 |
| O09 | split protocol | — | — | 供UI/backend分離的split text | mtr概念 | 若未來helper/UI分離，使用版本化length-prefixed/Named Pipe protocol，不直接採無schema文字split | P3 | 未開始 |
| O10 | wide report | UI靠自動欄寬，TXT固定tab | — | `--report-wide`依最長host | mtr | 純文字檔提供`aligned`與`tsv`兩種；clipboard預設TSV較可靠 | P3 | 未開始 |
| O11 | field order | 固定stable14欄 | 固定9欄 | 可自訂fields | mtr | CSV/JSON stable schema；text/report可跟使用者欄profile | P2 | 未開始 |
| O12 | ECMP完整輸出 | 不受UI display limit，輸出所有responders | 無ECMP | report最多display limit；raw可看到事件 | Hyper | 保留，並在JSON為每responder輸出reply_count與metadata source | 保留/P2 | 部分完成 |
| O13 | unknown hop輸出 | snapshot逐hop輸出，不受UI合併影響 | 每hop | 每hop`???` | Hyper | 保留原始逐TTL；另可輸出`display_groups`但不可取代raw hops | 保留 | 完成（既有） |
| O14 | 數值型別 | JSON數值正確但無null；無reply時best等寫0，0ms和missing難分 | 不適用 | JSON數值，未知host另外字串 | 自訂 | 無樣本的RTT欄輸出`null`；UI空白；不要用0混淆 | P1 | 完成|
| O15 | locale與decimal | `std::format`未指定locale，machine format通常`.` | 基本文字 | C.UTF-8與printf | 自訂 | machine formats永遠`.`與ASCII field keys；display text才採zh-TW locale | P1 | 完成|
| O16 | JSON Unicode | 寬字串轉UTF-8；control escape，但手寫writer需測surrogate/non-BMP | — | Jansson處理 | mtr | 改正式writer並以emoji、CJK extension、unpaired surrogate fuzz | P1 | 完成|
| O17 | HTML安全 | 所有cell/target escape `&<>"'` | 基本escape需另驗 | XML/JSON有專用library/escape | Hyper/mtr | 保留；URL不能直接由provider資料注入href | 保留 | 完成（既有） |
| O18 | CSV安全 | RFC式quote，但沒有formula injection防護 | — | raw `printf`CSV escaping不足 | Hyper | 增加Excel-safe選項；原始machine CSV不能默默改資料，可另提供`excel_csv`profile | P2 | 未開始 |
| O19 | screenshot資料範圍 | 擷取目前完整外窗，不等同全部被scroll遮住的表格 | — | — | Hyper | 選單區分「視窗截圖」與「完整報表圖片」；後者離屏render所有rows且設像素上限 | P3 | 未開始 |
| O20 | 檔案寫入錯誤 | bool後顯示一般錯誤；直接CREATE_ALWAYS會先截斷 | 類似 | stdout/close_stdout檢查write failure | mtr | 保留Win32 error code並顯示路徑/原因；原子寫入、磁碟滿、唯讀、長路徑測試 | P1 | 程式完成／磁碟滿、唯讀、長路徑故障注入待測|
| O21 | 檔案命名 | 使用Save dialog | 同 | stdout由shell | Hyper | 建議default filename含target-safe、UTC timestamp、family；清理Windows保留字 | P3 | 未開始 |
| O22 | schema相容性 | 無version/JSON schema | 無 | legacy formats相對穩定 | mtr | 發布`docs/schema/winmtr-report-v1.json`與golden files；只能向後相容加optional欄，破壞則升major | P1 | 完成|
| O23 | 分享前匿名化 | — | — | — | **自訂** | 提供隱藏本機/公網IP末段、target、hostname的預覽式redaction；永不自動改原始量測 | P2 | 未開始 |
| O24 | 匯入/重播 | — | — | raw backend概念存在，TODO提log/replay | **自訂** | 先支援讀取本身versioned NDJSON/JSON作離線檢視；能大幅改善bug重現與UI測試 | P3 | 未開始 |
| O25 | clipboard失敗原子性 | 先EmptyClipboard，再逐format放；第二format失敗可能只留下文字 | 類似 | 不適用 | 自訂 | 先配置所有HGLOBAL，Open後一次提交；HTML失敗仍以文字成功提示或完全回滾策略明確 | P2 | 未開始 |

## 11. 併發、安全、錯誤處理、資源與效能

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| Q01 | 最小權限 | Windows ICMP API + asInvoker | 同 | helper取得raw capability後drop uid/gid/capabilities | 協定依賴 | 保持ICMP asInvoker；raw transport才採helper隔離 | 保留 | 完成（既有） |
| Q02 | UI thread安全 | worker只commit model/PostMessage，UI取snapshot | coroutine context切換較複雜 | UI event loop單thread | Hyper | 保留message/snapshot邊界；所有callback持weak window token | 保留/P1 | 完成|
| Q03 | model鎖 | 單mutex保護host/cache/revision；network lookup完成可能持鎖遍歷64 hops | recursive mutex | UI net大多單thread；helper另process | mtr/自訂 | probe完成critical section只做O(1)更新；metadata fan-out先收集key再短鎖更新，禁止鎖內I/O | P1 | 完成|
| Q04 | detached thread | 每個metadata lookup `std::thread(...).detach()`，shared_ptr保owner存活但無中央停止/上限 | fire-and-forget/WinRT | resolver child與event loop有owner | mtr | 改bounded `std::jthread` pool/queue；session stop取消未開始job，已開始job有deadline | P1 | 完成|
| Q05 | object lifetime | `weak_from_this().lock()`後thread持shared self，安全但可延長WinMTRNet到網路timeout後 | coroutine靠shared owner/context | process/struct明確生命週期 | 自訂 | app shutdown有TaskGroup；不靠隱性shared_ptr延命，owner await/join所有任務 | P1 | 完成|
| Q06 | async ICMP UAF風險 | 同步buffer安全 | ⚠ timer resume與OS completion可能競爭 | pending heap object直到完成 | mtr/自訂 | 不採Refresh awaiter；新request state machine需TSAN式競爭審查與壓力測試 | **P0** | 完成 |
| Q07 | handle RAII | custom `unique_icmp_handle`、WinHTTP/DNS unique_ptr | winrt handle wrappers | C手動close/free，有集中path | Hyper | 保留RAII；全部Win32 handle採同一typed unique_handle utilities | 保留/P2 | 部分完成 |
| Q08 | stop token傳播 | trace/public info有stop_token；detached metadata沒有 | trace有stop_token | signal/pipe close | Hyper + 自訂 | request/task均帶session stop token；WinHTTP handle close作實際取消 | P1 | 完成|
| Q09 | stale callback | session/epoch拒絕舊trace與metadata；dialog generation拒絕舊finished | 缺少同等完整gate | process/sequence天然隔離 | Hyper | 保留並以測試覆蓋reset/stop/restart race | 保留/P1 | 完成|
| Q10 | lock與callback重入 | `notifyTraceDataChanged`在worker commit後執行，不在model鎖中 | WinRT continuation可能重入 | event loop | Hyper | 定義規則：鎖內不PostMessage、不呼provider、不resolve、不format | P1 | 完成|
| Q11 | 外部輸入大小 | HTTP body1MiB、string/JSON基本限制；hostname由Windows API限制 | 基本 | command buffer4096、parser驗證 | mtr | 對hostname、provider strings、TXT records、export cell設合理上限並保留truncated標記 | P1 | 完成|
| Q12 | command injection | GUI不spawn shell | 同 | mtr command pipe是自訂token parser，已有parser tests/fuzz | Hyper | headless/ helper切勿組shell命令；Named Pipe binary/strict protocol | 保留/P1 | 完成（目前不 spawn shell／無 helper command protocol） |
| Q13 | 權限分離攻擊面 | 無helper/raw parser，攻擊面小 | 同 | `mtr-packet` parser/raw decoder是高風險面，但有drop/fuzz/tests | Hyper | 不為了功能對齊就引入raw parser；每新增protocol需威脅模型 | 保留/P2 | 部分完成 |
| Q14 | sudo/UAC誤用 | 不需UAC | 不需 | SECURITY明確反對讓整個mtr經sudo獲權限 | mtr原則 | README明確寫「不要以系統管理員執行」；未來helper只給最小權限 | P2 | 未開始 |
| Q15 | 配置資料驗證 | registry load clamp；UI再驗 | DDV/registry | CLI strict parse | Hyper/mtr | registry schema每欄type/range，未知新欄忽略；CLI不得silent clamp | P1 | 完成|
| Q16 | local resource exhaustion | 每round最多64 threads；長timeout因被cap目前較小 | 30 coroutines/threadpool waits | max10,240 outstanding | mtr | 新scheduler hard cap、handle cap、metadata queue cap；資源不足是local error，不算loss | **P0** | 完成 |
| Q17 | rate limiting | 0.1s×64 hops可達640pps且首輪burst；無總pps上限 | 最小1s×30 | 非root interval>=1；分散發送 | mtr | 一般GUI全域上限如100pps，進階需確認；均勻spacing，顯示估算pps | P1 | 完成|
| Q18 | amplification至外部metadata | 新公網responder可各開HTTPS thread；最多128×64潛在地址 | 無metadata | DNS cache/hash | 自訂 | fixed concurrency、每provider QPS、dedupe跨TTL/session、circuit breaker | P1 | 完成|
| Q19 | overflow | 64-bit counters安全；`interval*1000` cast先clamp但需測極值 | 32-bit counters | 多個int counter | Hyper | 保留checked conversions；時間乘法用chrono/uint64，不接受NaN/Inf interval | P1 | 完成|
| Q20 | exception邊界 | trace/dialog捕全部；部分noexcept內allocation需留意terminate | coroutine exceptions可能由IAsyncAction傳遞 | C error/exit | 自訂 | no-throw UI message handler只做有界工作；allocation/I/O函式不要錯標`noexcept`，集中轉ErrorCode | P1 | 完成|
| Q21 | 可觀察錯誤 | 多數AfxMessageBox一般訊息 | 多個英文status字串混在host | stderr細緻錯誤 | mtr | status/detail顯示error category/code/action；debug log含HRESULT/Win32 code但遮敏 | P1 | 部分完成（export/CLI 有錯誤碼；完整診斷 log 待補）|
| Q22 | crash recovery | 無自動保存session | 無 | CLI重跑 | 自訂 | 不需自動上傳；可選minidump+last sanitized event ring，使用者主動匯出 | P3 | 未開始 |
| Q23 | HTML/JSON注入 | HTML escape；JSON escape基本 | 基本 | Jansson/XML escape | Hyper/mtr | 正式serializer+fuzz；CSV formula另處理 | P1 | 完成|
| Q24 | DNS rebinding/SSRF | provider host/path編譯期固定；per-IP只是URL path encode | 無 | DNS provider suffix可自訂 | Hyper | 保持HTTPS provider allowlist；不要讓公網回傳值成下一個任意URL | 保留 | 完成（既有） |
| Q25 | TLS降級 | 允許TLS1.0/1.1是安全債 | WinRT/OS預設 | 不適用 | 自訂 | 如N35，最低TLS1.2並依OS default；Windows7缺TLS1.2時功能失敗但trace仍可用 | P1 | 完成|
| Q26 | binary hardening | 未在CMake完整明列CFG/CET/SDL等；VS預設依toolset | 同 | OS/toolchain flags | 自訂 | Release啟用ASLR/DEP/CFG、`/guard:cf`、`/sdl`；能用時CET；CI用dumpbin驗證 | P2 | 未開始 |
| Q27 | code signing | 專案未見自動簽章流程 | 未見 | distro簽署 | 自訂 | 正式release簽Authenticode、發布SHA-256與provenance；私鑰由CI secret/HSM保管 | P1 | 外部 gate（需受保護 Authenticode 憑證/HSM）|
| Q28 | SBOM/依賴追蹤 | vcpkg空、系統imports | cppwinrt依賴 | 多可選libs | 自訂 | release生成SPDX/CycloneDX與Windows imports；即使零第三方也保留產物 | P2 | 未開始 |
| Q29 | 效能profiling | 無benchmark/telemetry | 無 | 長期使用但無正式bench suite | 自訂 | 建立10/30/64 TTL、10Hz、10s timeout、50%loss的scheduler benchmark與memory ceiling | P1 | 完成|
| Q30 | 休眠/喚醒與時鐘跳變 | GetTickCount64/steady clock能避wall-clock跳變；sleep後可能立即大量cycle | WinRT timer | timeval | Hyper + 自訂 | 偵測resume或lateness>interval×2後重新anchor，不補發睡眠期間slot；觸發public info network refresh | P2 | 未開始 |

## 12. 測試、CI、fuzz、文件與維護流程

| ID | 比較項目 | Hyper | Refresh | mtr | 最佳基準 | 對 Hyper 的建議實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|---|
| T01 | unit tests | —，沒有test目錄/target | — | Python與C tests：parser、params、probe、format等 | mtr | 先測純model/scheduler/serializers/prefix/provider parsing；CTest整合 | **P0/P1** | 完成 |
| T02 | scheduler deterministic tests | — | — | event loop有行為測試但非完整virtual clock unit suite | **自訂** | fake clock + fake transport；可精確推進send/reply/timeout，驗證1秒interval與3秒timeout並存 | **P0** | 完成 |
| T03 | probe integration tests | 依手動實機 | 依手動實機 | loopback、remote IPv4/6、多protocol、packet listener | mtr | Windows loopback/local mock service驗證v4/v6、status、size、TOS、DF；internet smoke test只排程 nightly | P1 | 完成（離線 IPv4／IPv6 Windows ICMP loopback） |
| T04 | packet parameter tests | — | — | size/pattern/TOS、IPv6等 | mtr | 對Windows API adapter用mock驗證參數；實機用packet capture只在隔離CI runner | P1 | 完成|
| T05 | privilege tests | 不需提升但未自動驗證 | 同 | capability-drop static test與runtime策略 | mtr | CI用標準user啟動smoke，manifest/import檢查不得requireAdministrator | P1 | 完成（asInvoker/import gate + 無提升 loopback CTest） |
| T06 | parser fuzz | — | — | command parser libFuzzer | mtr | CLI parser、JSON/provider parser、report import均建立fuzz target | P1 | 完成（CLI value + JSON/provider offline fuzz；尚無 report import 功能） |
| T07 | packet fuzz | 不解析raw packet | 不解析raw packet | IPv4/6、ICMP/error queue、MPLS fuzz+seed corpus | mtr（raw時） | 現在不需複製；若加raw transport，功能合併前必須先有同級fuzz | P3 | 未開始 |
| T08 | serializer golden tests | — | — | report formats有既有行為但本樹未見完整golden matrix | **自訂** | 對TXT/HTML/CSV/JSON建立Unicode、ECMP、unknown、no-reply、large counters golden files與schema validation | P1 | 部分完成（JSON schema/golden 與 escape tests；四格式同 fixture matrix 待補）|
| T09 | UI layout tests | 手動 | 手動 | 不適用/GTK手動 | **自訂** | Windows VM screenshot tests：96/120/144/192 DPI、800×600至4K、長hostname/ISP、100 rows、高對比 | P1 | 外部 gate（需互動 Windows VM screenshot matrix）|
| T10 | race/stress tests | — | — | event-loop/ASAN可測 | **自訂** | 1萬次start/stop/reset/restart；reply與reset同時；network change與close同時；Application Verifier/ASAN | **P0/P1** | 核心壓力完成／Application Verifier 實機待外部驗證|
| T11 | timeout/cadence驗收 | 目前沒有，故用`min(timeout, interval)`修補後仍可錯判slow reply | 無，固定5秒 | 行為成熟但無Windows測試 | **自訂** | 必測：interval=1s、timeout=3s、silent 30 hops持續60s；每TTL約60 sends，inflight≤3/TTL，3秒reply仍算成功 | **P0** | 完成 |
| T12 | route change tests | — | — | netem tests與dynamic behavior | mtr | fake transport腳本模擬route 8→15→6、destination silent、ECMP churn、5/12 unknown；驗證ceiling/hysteresis | P1 | 完成|
| T13 | Linux netem | 不適用native Windows | 不適用 | `test/linux/netem*.py` | mtr概念 | Windows可用本機fake transport作主測；另用Linux/mtr作參考oracle，不必在Windows引入netem | P2 | 未開始 |
| T14 | CI build | GitHub Action只有Ubuntu MegaLinter，不能編譯MFC | 同 | Ubuntu compile、sample、cmdparse | mtr | 新增windows-2022 MSVC+CMake x64 Debug/Release build，cache但不依賴developer machine | **P0/P1** | 完成 |
| T15 | CI test coverage | 無 | 無 | workflow只明確跑cmdparse和live sample，雖`make check`有更多test但CI未全跑 | 自訂 | Windows CI跑全部unit/golden；mtr也提醒「有tests≠CI全執行」 | P1 | 完成|
| T16 | linter | MegaLinter C/C++ flavor，VALIDATE_ALL_CODEBASE=false，且可自動開fix PR | 同 | flake8；另有test/lint.sh | 自訂 | 保留format/lint但PR至少lint changed files；main/nightly全庫；不要讓linter成唯一CI | P1 | 完成|
| T17 | GitHub Action pinning | actions pin commit，較供應鏈安全 | 同 | 使用較舊unpinnedmajor actions v3/v4 | Hyper | 保留commit pin並用Dependabot更新；最小permissions，linter不需長期contents write時取消 | P1 | 完成|
| T18 | CI permissions | MegaLinter job給contents/issues/PR write以自動fix，權限偏大 | 同 | 一般read jobs | mtr原則 | lint/build jobread-only；自動fix另獨立受保護workflow且只在trusted branch | P1 | 完成|
| T19 | live network CI | 無 | 無 | sample打1.1.1.1，可能flaky/政策敏感 | 自訂 | PR不依賴公網；nightly optional smoke可標non-blocking並有明確target同意 | P1 | 完成|
| T20 | Windows版本matrix | README要求手動Win7/10/11 | 未見CI | 不適用 | Hyper需求 | Win11/Server2022自動；Win10、Win7 SP1以self-hosted/發布前VM；每版留測試紀錄 | P1 | 外部 gate（Win7 SP1／Win10／Win11 VM 記錄）|
| T21 | ARM64/Win32 CI | 無，雖VS project列組態 | 無 | 多Unix arch由生態 | 自訂 | 若不支援就刪組態；若支援則CI build+device smoke，不能維持幽靈組態 | P1 | 完成|
| T22 | sanitizer CI | 組態存在但未跑 | 同 | configure可ASAN/UBSAN，workflow未見全矩陣 | 自訂 | nightly Windows ASAN；純core另可Clang-cl UBSAN可行性評估 | P1 | 完成|
| T23 | static analysis | MegaLinter可能含cppcheck/clang-format，Windows modules支援有限 | 同 | lint/Bandit等 | 自訂 | MSVC `/analyze`與CodeQL C++（能建置modules後）；網路 parser/Win32 handle規則加自訂check | P2 | 未開始 |
| T24 | coverage | — | — | 未見CI coverage gate | **自訂** | core unit tests產OpenCppCoverage/VS coverage；先追關鍵分支而非盲目百分比，P0 scheduler≥90% branch | P2 | 未開始 |
| T25 | README使用者文件 | 繁中、支援矩陣、功能、來源、CLI、build完整 | 較短 | man page/README詳盡 | Hyper+mtr | 保留README；所有CLI/公式/timeout語意移到`docs/`詳述，README只作入口 | P1 | 完成|
| T26 | man/help同步 | resource/help/README多個手動點 | help較少 | code註記要求long option、man、usage同步 | mtr | 以一份option descriptor生成CLI help、GUI range tooltip與docs table；CI比對 | P2 | 未開始 |
| T27 | customization文件 | 有`CUSTOMIZATION.md`，品牌/來源/版本位置清楚 | — | configure/man | Hyper | 保留；改成由單一配置生成後更新文件 | 保留 | 完成（既有） |
| T28 | security文件 | README提一般權限/TLS，但無獨立SECURITY政策 | 無同級 | `SECURITY`詳述capability、setuid、sudo風險 | mtr | 增加`SECURITY.md`：回報管道、支援版本、外部資料、權限、敏感資訊處理、簽章 | P1 | 完成|
| T29 | threat model | — | — | SECURITY偏權限分析 | **自訂** | 文件化assets/trust boundaries：target DNS、ICMP replies、HTTP JSON、DNS TXT、registry、export、future helper | P2 | 未開始 |
| T30 | changelog/release notes | Git history，無本版正式CHANGELOG | Git history/releases | `NEWS`完整歷史 | mtr | 建`CHANGELOG.md`，量測語意變更（timeout/interval/stat formula）必列breaking/behavior change | P1 | 完成|
| T31 | issue template | 有bug report template | 有 | repo生態 | 三者 | template新增版本/commit、OS/DPI、options、target是否可公開、sanitized JSON report、重現時間 | P2 | 未開始 |
| T32 | benchmark/regression gate | — | — | — | **自訂** | scheduler cadence、CPU、memory、UI frame time建立baseline，PR超閾值提示 | P2 | 未開始 |
| T33 | reproducible fixture | — | — | packet listener/netem接近 | mtr | 建`ScriptedProbeTransport` JSON場景；同時供unit、UI demo、bug reproduction | **P0/P1** | 部分完成（scripted scheduler/route；JSON fixture/UI demo 待補）|
| T34 | upstream同步策略 | 由Refresh分支大幅擴張，沒有明確upstream merge文件 | 本身上游 | mtr獨立 | 自訂 | 不再做盲目整樹merge；建立`UPSTREAM_NOTES.md`追蹤選擇性port、來源commit與偏離理由 | P2 | 未開始 |
| T35 | release gate | README列人工清單，未自動化 | 未見 | distro/CI | 自訂 | release checklist自動驗證build/tests/schema/signature/SBOM/Win7 imports；人工只剩實機UI | P1 | 自動 build/tests/schema/SBOM/import/hash 完成／Authenticode、舊版 Windows VM 為外部 gate |

## 13. 功能聯集／差集：其他專案有、Hyper 沒有

下表專門列出上游獨有或較完整的能力。不是所有功能都值得搬進 Windows GUI；「不移植」也是經評估後的建議。

| ID | 上游能力 | 來源 | Hyper現況 | 是否建議 | 對 Hyper 的實作方式 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|
| G01 | interval與timeout完全解耦的pending probes | mtr | 沒有 | **必要** | 新Windows async scheduler；見第15節 | **P0** | 完成 |
| G02 | TTL間均勻spacing而非同時burst | mtr | 沒有 | **必要** | interval內平均安排每hop slot | **P0** | 完成 |
| G03 | 每TTL獨立長駐循環 | Refresh | 現為每round threads | 採意圖、不採程式 | 用central scheduler達成，不照搬WinRT awaiter | **P0** | 完成 |
| G04 | UDP probing | mtr | 沒有 | 建議 | 以獨立transport plugin；Windows先評估UDP socket + ICMP error取得限制 | P2 | 未開始 |
| G05 | TCP probing | mtr | 沒有 | 建議 | 非阻塞TCP connect + TTL socket option；明確區分connect result/timeout/RST | P2 | 未開始 |
| G06 | SCTP probing | mtr | 沒有 | 暫不建議 | Windows原生支援與使用率有限；保留extension interface即可 | P3 | 未開始 |
| G07 | MPLS ICMP extension | mtr | 沒有 | 有條件建議 | Windows ICMP API是否保留extension需先實驗；若需raw parser則獨立helper+fuzz | P3 | 未開始 |
| G08 | 指定source interface/address | mtr | 沒有 | 建議 | Windows adapter enumeration + source address；IPv4用IcmpSendEcho2Ex source、IPv6驗證API | P2 | 未開始 |
| G09 | local/remote port | mtr | 沒有 | 隨UDP/TCP加入 | transport-specific options，ICMP時disable | P2 | 未開始 |
| G10 | Linux SO_MARK | mtr | 沒有 | 不直接移植 | Windows沒有相同通用語意；用interface/source/compartment解決可解的需求 | P3 | 未開始 |
| G11 | due TTL | mtr | 只有minimum TTL近似 | 建議 | 統一成「至少探到TTL」的路徑停止規則 | P1 | 完成|
| G12 | random packet size | mtr | 只有random pattern | 建議 | 增加min/max/seed；匯出每包實際size或session策略 | P2 | 未開始 |
| G13 | grace time | mtr | Stop只drain當前round | 必要 | 發送停止後等待pending至deadline/grace，顯示inflight | P1 | 完成|
| G14 | pause/resume | mtr | 沒有 | 建議 | scheduler pause新send，pending照常完成，resume重新anchor | P2 | 未開始 |
| G15 | RTT 400點歷史 | mtr | 沒有 | 建議 | per-hop bounded ring buffer | P2 | 未開始 |
| G16 | block/braille latency graph | mtr | 沒有 | 部分移植 | GUI採sparkline/heatmap，不搬terminal glyph UI | P3 | 未開始 |
| G17 | custom fields/order | mtr | 固定14欄 | 建議 | header拖拉+column chooser+profiles | P2 | 未開始 |
| G18 | host name與IP同時顯示 | mtr | 二選一 | 建議 | display mode：name/IP/both，model不變 | P2 | 未開始 |
| G19 | Drop/Gmean/Javg/Jmax/Jint | mtr | 只有loss/avg/jitter/stddev | 部分建議 | Drop進details/export；Javg/Jmax/Jint可選；Gmean進階 | P2/P3 | 未開始 |
| G20 | XML輸出 | mtr | 沒有 | 暫緩 | 除非有明確consumer；JSON schema優先 | P3 | 未開始 |
| G21 | raw/split輸出 | mtr | 沒有 | 改採現代版本 | NDJSON event stream；helper用versioned pipe protocol | P2/P3 | 未開始 |
| G22 | headless report | mtr | 沒有 | 建議 | 共用core+serializers的console entry point | P2 | 未開始 |
| G23 | 多目標batch與file | mtr | 沒有 | 建議給headless | sequential jobs+per-target result | P3 | 未開始 |
| G24 | build feature列印 | mtr | 沒有 | 建議 | `--diagnostics/--version --verbose` | P2 | 未開始 |
| G25 | bash completion | mtr | 沒有 | 替換實作 | 提供PowerShell/winget，bash只在WSL package需要 | P3 | 未開始 |
| G26 | privilege-separated helper | mtr | 沒有且現不需要 | 未來條件式 | 只在raw協定時建最小helper；ICMP不引入 | P2/P3 | 未開始 |
| G27 | capability drop/security policy | mtr | asInvoker但無SECURITY.md | 建議原則 | 新增security policy；raw helper才實作capability等價 | P1 | 完成|
| G28 | parser/probe/netem tests | mtr | 沒有 | **必要** | Windows fake transport+loopback integration | **P0/P1** | 完成 |
| G29 | fuzz targets/corpus | mtr | 沒有 | 建議 | 先fuzz parsers/serializers；raw packet功能前fuzz packet parser | P1/P3 | 完成（現有 CLI value／JSON／provider／serializer 邊界） |
| G30 | NAT64 ASN處理 | mtr | 沒有 | 建議 | RFC6052 well-known prefix+測試 | P2 | 未開始 |
| G31 | ASN route/registry/allocated欄 | mtr | 只有country/asn/ISP | 可選 | metadata details/JSON新增，不擠主表 | P3 | 未開始 |
| G32 | report開始時間 | mtr | 沒有 | 建議 | snapshot與所有exports加入UTC時間 | P1 | 完成|
| G33 | sample standard deviation | mtr | population stddev | 建議 | 改N-1並version統計定義 | P1 | 完成|
| G34 | hard outstanding cap | mtr | round天然上限但新架構需要 | **必要** | 全域/每TTL上限、backpressure/local drop | **P0** | 完成 |

## 14. 功能聯集／差集：Hyper 有、兩個比較專案沒有或明顯較弱

| ID | Hyper能力 | Refresh | mtr | 建議判斷 | 對 Hyper 的後續實作 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|---|
| H01 | 完整繁體中文台灣介面 | 英文為主 | 英文CLI | **保留** | 新字串一律resource化、加詞彙表 | 保留 | 完成（既有） |
| H02 | Windows 7 SP1至11 desktop API相容層 | 偏Win10/11 OneCore/WinRT | 非native Windows GUI | **保留** | runtime resolve新API，發布矩陣實測 | 保留/P1 | 程式完成／Win7 SP1 VM 待外部驗證|
| H03 | C++23 immutable trace snapshot+revision | 基本vector getter | 全域model/event loop | **保留** | 作為UI/export唯一讀取入口 | 保留 | 完成（既有） |
| H04 | session id + data epoch防stale | 較弱 | sequence/process隔離 | **保留** | 強型別ID並全race測試 | 保留/P1 | 完成|
| H05 | 可設定timeout/cycles/max hops/start/minTTL/unknown | 多數沒有 | CLI多數有，但Refresh沒有 | **保留** | CLI補齊、語意校正 | P1 | 完成|
| H06 | IPv4+IPv6允許並依實際可用性fallback最多8候選 | resolver單路徑 | 第一個addrinfo | **保留並改善** | Happy-Eyeballs式受控競賽，顯示selected address | P1 | 完成|
| H07 | 128 ECMP responders+獨立顯示上限 | 無ECMP | 有128 paths但metadata/匯出整合較分散 | **保留** | 加stable responder stats/share | P1/P2 | 完成|
| H08 | unknown range合併 | 每hop | 每hop | **保留** | 可展開、統計語意精確 | P2 | 未開始 |
| H09 | Country/ASN/ISP主表14欄 | 無 | ASN/ipinfo可選但非同等Windows UI | **保留** | source/status/queried-at進details | P2 | 未開始 |
| H10 | hop metadata 24h bounded cache | 無 | session hash無明確TTL | **保留並修正** | 正負TTL分離、thread pool/rate limit | P1 | 完成|
| H11 | reply cache可設定 | 無 | 有類似`--cache` | **保留** | 預設關、顯示Skipped、path change invalidation | P1 | 完成|
| H12 | 公網IPv4/IPv6資訊 | 無 | 無 | **保留** | provider abstraction、parallel budget、privacy consent | P1 | 完成|
| H13 | 遞迴DNS/ECS資訊 | 無 | 無 | **保留** | last-updated/source/stale state | 保留/P2 | 部分完成 |
| H14 | 本機DNS server清單 | 無 | 無主介面 | **保留** | adapter關聯放details | P2 | 未開始 |
| H15 | network-change/fixed interval公網refresh | 無 | 無 | **保留** | manual refresh+backoff | P1 | 完成|
| H16 | primary/fallback不混合來源政策 | 無 | 單DNS provider | **保留** | provider result與policy分層，逐section source | 保留/P1 | 完成|
| H17 | 公網/節點資訊台灣country name | 無 | country code | **保留** | prefix/Geo DB測試 | P2 | 未開始 |
| H18 | 內容導向主窗自動寬高 | 固定/basic | terminal/GTK | **保留但尊重使用者** | 首次自動，手動resize後不反覆搶尺寸 | P1 | 完成|
| H19 | per-monitor DPI欄寬重算 | 較弱 | 非MFC | **保留** | 多monitor自動化測試 | P1 | 完成|
| H20 | 系統背景色整合與group-box修正 | 原版有差異 | N/A | **保留** | 高對比fallback | P1 | 完成|
| H21 | 狀態列company hyperlink且無文字重疊 | 原版較易重疊 | 無 | **保留** | accessibility與focus | P2 | 未開始 |
| H22 | target追蹤時隱藏selection | 原版行為較弱 | N/A | **保留** | UI test | 保留 | 完成（既有） |
| H23 | 主/ECMP/unknown節點詳細dialog | 只有基本主hop | TUI文字 | **保留** | 加每responder統計、error/source | P2 | 未開始 |
| H24 | 公網資訊可resize、可複製詳細dialog | 無 | 無 | **保留** | 改grid而非靠tab文字 | P2 | 未開始 |
| H25 | TXT/HTML/CSV/JSON四種匯出 | TXT/HTML | report/CSV/JSON/XML/raw，沒有Hyper GUI整合 | **保留** | versioned schema與atomic write | P1 | 完成|
| H26 | Clipboard HTML標準fragment | 有基本 | 無 | **保留** | golden test與transaction改善 | P2 | 未開始 |
| H27 | 全窗screenshot到clipboard | 無 | 無 | **保留** | 分視窗/完整報表image | P3 | 未開始 |
| H28 | 分享前100包提醒 | 無 | cycles由使用者 | **保留** | 可override、勿阻礙CLI | P2 | 未開始 |
| H29 | 品牌/來源/字型集中設定 | 較分散 | build/configure集中但非產品branding | **保留** | 改單一生成source避免版本多點 | P2 | 未開始 |
| H30 | 公開位址特殊prefix過濾 | 無 | 無同級HTTP gate | **保留** | table-driven+registry更新測試 | P1 | 完成|
| H31 | 失敗provider只在完全無usable result時fallback | 無 | 單provider | **保留** | 失敗reason/circuit breaker | P1 | 完成|
| H32 | 完整ECMP匯出不受UI limit | 無 | report受display path設定 | **保留** | schema清楚分raw responders/display rows | 保留 | 完成（既有） |

## 15. 三個版本都沒有完整做到，但本修改版應自行實作

| ID | 缺少的能力 | 三者現況 | 建議設計 | 驗收標準 | 優先級 | 實作狀態 |
|---|---|---|---|---|---|---|
| X01 | 安全的Windows原生nonblocking MTR scheduler | Hyper同步barrier；Refresh async生命週期有風險；mtr語意正確但非Win32 ICMP adapter | `ProbeRequest`穩定生命週期 + completion queue + deadline heap + token + bounded inflight | interval=1s/timeout=3s時，silent hop仍每秒送；3秒內reply都計成功；ASAN/stress無UAF | **P0** | 完成 |
| X02 | transport可替換與fake clock | 三者核心均與OS/event loop耦合 | `IProbeTransport`、`IClock`、`ISchedulerSink` interfaces；production Windows adapter與scripted fake | 不連網即可重現route/loss/late reply/cancel | **P0** | 完成 |
| X03 | 完整per-responder統計 | 三者主要都聚合TTL | responder reply count/RTT stats/share；sent/loss只在有可歸因設計時顯示 | ECMP兩地址可各自看到RTT與reply share，總和一致 | P2 | 未開始 |
| X04 | 版本化量測schema | 三者輸出都有歷史格式但缺完整行為version | JSON schema v1，公式、options、timestamps、outcomes、responders、events | schema validator+golden tests；舊reader規則文件化 | P1 | 完成|
| X05 | 可重播診斷session | mtr raw接近，其餘無 | NDJSON event recording + offline replay view | bug report可不連網重現相同UI/model | P3 | 未開始 |
| X06 | route change event timeline | 都只留目前/歷史地址 | bounded event log，記dominant/path frontier變更 | route 8→15→6有時間與原因，不污染stats | P2 | 未開始 |
| X07 | Lifetime + sliding window統計 | 都以全程為主 | bounded sample ring，明確切換Lifetime/last N/last minutes | loss/percentile視窗標示清楚且可測 | P3 | 未開始 |
| X08 | privacy-aware export | 都無完整匿名化 | 預覽式redaction profiles + raw unchanged option | anonymized report不含target/public/private完整IP，仍保留診斷拓樸 | P2 | 未開始 |
| X09 | provider可觀察性與backoff | Hyper有fallback但無health；其餘較少 | provider state machine：healthy/cooldown/probing，錯誤分類、last success | provider掛掉不會對每hop反覆打；UI可看到stale | P1 | 完成|
| X10 | GUI accessibility驗收 | 三者都無完整Windows UIA測試 | UIA names/roles、keyboard、high contrast、focus order | Accessibility Insights核心流程無阻斷問題 | P2 | 未開始 |
| X11 | 自動release assurance | 三者本比較commit都不完整 | Windows build/test/sign/SBOM/schema/VM checklist pipeline | release artifact可追溯commit、簽章、hash，支援矩陣有紀錄 | P1 | CI artifact/SBOM/import/hash 完成／Authenticode 與 Windows VM 為外部 gate |
| X12 | metric definition文件 | 欄名相似但jitter/stddev公式不同 | `STATISTICS.md`逐欄公式、單位、sample/inflight/cache語意 | UI tooltip、JSON formula version與文件一致 | P1 | 完成|
| X13 | local scheduler error與network loss雙軌 | 三者UI多半只呈現loss或stderr | outcome counters：reply/timeout/icmp_error/local_error/skipped/late/inflight | 資源不足/API錯誤絕不提高網路loss | **P0/P1** | 完成 |
| X14 | 休眠／網路切換session政策 | 都不完整 | resume後reanchor；interface change時標route boundary或提示restart | 睡眠後不burst，換網路前後數據不無標記混合 | P2 | 未開始 |
| X15 | deterministic performance budget | 都沒有明確gate | benchmark 64 TTL×10Hz×10s timeout，限制threads/handles/memory/UI frame | 符合預設budget且CI可比較regression | P1 | 完成|

## 16. 建議的 Windows 探測核心：具體資料流與狀態機

這一節不是把 mtr 的 Unix socket code直接搬進MFC，而是保留其「發送排程與request timeout獨立」的核心語意，改用Windows適合的transport。

### 16.1 元件切分

| 元件 | 責任 | 不可做的事 | 建議介面／資料 | 來源選擇 | 實作狀態 |
|---|---|---|---|---|---|
| `ProbeScheduler` | 依monotonic clock決定每TTL下一個send slot；管理deadline heap、cycles、pause、grace、backpressure | 不直接操作MFC control、不做DNS/HTTP、不計算欄寬 | `schedule(now)`, `onCompletion(event)`, `nextDeadline()` | mtr語意 + 自訂C++ | 未開始 |
| `IProbeTransport` | issue request、回報OS completion、宣告capabilities | 不更新route/stats、不決定timeout是否算loss | `issue(ProbeSpec, ProbeId) -> RequestHandle`、completion callback | 自訂Win32 abstraction | 未開始 |
| `WindowsIcmpTransport` | 包裝`IcmpSendEcho2Ex/Icmp6SendEcho2`非同步Event模式；維持buffer/event/handle生命週期 | 不相信async API的`Timeout`參數、不在callback碰UI | 每request stable storage；threadpool wait只enqueue `ProbeId` | Microsoft API + 自訂lifetime | 未開始 |
| `ProbeRequestStore` | 以ProbeId保存所有inflight/request resource；限制容量 | logical timeout時不可立刻釋放OS仍可能寫入的buffer | unordered_map + per-TTL index + state | mtr pending list概念 | 未開始 |
| `RouteModel` | 將ProbeOutcome套用到TTL/responder/stats；路徑ceiling/frontier/hysteresis | 不等待OS、不呼provider | immutable `TraceSnapshot`、route events | Hyper snapshot + mtr path behavior | 未開始 |
| `MetadataService` | PTR、ASN、country/ISP、cache、rate limit、provider health | 不阻塞scheduler、不可把local error算loss | bounded work queue、result含source/status/time | Hyper功能 + 自訂治理 | 未開始 |
| `UiPresenter` | 將snapshot diff成rows/cells；coalesce repaint | 不做probe、DNS、network query | stable RowKey、50–100ms frame budget | Hyper UI | 未開始 |
| `ReportSerializer` | versioned TXT/HTML/CSV/JSON/NDJSON | 不直接讀mutable model | 只接受immutable snapshot/session log | Hyper + 自訂schema | 未開始 |

### 16.2 每個 probe 的狀態

| 狀態 | 進入條件 | 統計動作 | 資源動作 | 下一狀態 |
|---|---|---|---|---|
| `scheduled` | TTL的`next_due`到達且未超cap | 尚不增加sent | 建立ProbeId/ProbeSpec | `issued`或`local_failed` |
| `issued` | OS已接受request | `sent++`, `inflight++` | request/event/reply buffer放入stable store；加入logical deadline heap | `completed`、`expired_waiting_os`或stop-drain |
| `completed` | OS completion在deadline前；parse有效 | `completed++`, `inflight--`；依outcome更新received/icmp error/RTT | 取消wait、等callback drain後釋放request | `retired` |
| `expired_waiting_os` | application deadline到達但OS尚未completion | `completed++`, `timed_out++`, `inflight--`；只做一次 | **仍保留** request/buffer/event，標記late結果不可寫統計 | `late_completed`或transport hard cleanup |
| `late_completed` | logical timeout後OS才完成 | 只增加debug `late_completions`，不回改loss | 安全解除wait並釋放 | `retired` |
| `local_failed` | API未接受、resource/capability錯誤 | `local_errors++`；不增加network sent/loss | 立即釋放未issue資源 | `retired` |
| `skipped_backpressure` | inflight達上限 | `scheduler_skipped++`；不算sent/loss | 不配置request | `retired` |
| `ignored_epoch` | reset/restart後完成 | 不更新新epoch | 只安全回收 | `retired` |

### 16.3 Windows async lifetime 的硬性規則

| 規則 | 原因 | 實作要求 |
|---|---|---|
| async mode的API `Timeout`不可作deadline來源 | Windows文件說明async呼叫不以該參數實作等待語意 | Scheduler自己維護deadline；API參數只填文件允許值，不依賴它喚醒 |
| logical timeout不等於OS operation已完成 | timer到時，kernel/API仍可能寫reply buffer或signal event | request storage不能回收/重用；只把量測結果finalize為timeout |
| callback只傳ProbeId，不持有可移動container內裸指標 | vector reallocation、session reset或stop會使指標失效 | request配置穩定地址；callback取得weak owner/token，將ID放入lock-free/MPSC queue |
| 關閉threadpool wait前drain callback | callback可能與destructor同時執行 | `SetThreadpoolWait(wait,nullptr,nullptr)`後`WaitForThreadpoolWaitCallbacks`，再刪context |
| transport hard-cancel必須有文件或實證保證 | 若`IcmpCloseHandle`未保證不再寫buffer，強制free仍可能UAF | 在Win7/10/11壓測驗證；缺保證時採bounded quarantine/backpressure，寧可延後回收不可UAF |
| Stop不阻塞UI | grace可能數秒 | UI只轉STOPPING並顯示pending；TaskGroup在背景drain，完成後PostMessage |

### 16.4 1 秒週期／3 秒 timeout 的預期時間軸

| 時間 | TTL 8的動作（假設完全無回覆） | TTL 8 in-flight | 統計 |
|---:|---|---:|---|
| 0.0s | 送Probe A，deadline 3.0s | 1 | sent=1, completed=0 |
| 1.0s | 送Probe B，deadline 4.0s | 2 | sent=2, completed=0 |
| 2.0s | 送Probe C，deadline 5.0s | 3 | sent=3, completed=0 |
| 3.0s | A logical timeout；同一slot送Probe D | 3 | sent=4, completed=1, timeout=1 |
| 4.0s | B timeout；送Probe E | 3 | sent=5, completed=2, timeout=2 |

這才同時滿足「一秒一包」與「每包可等待三秒」。Hyper目前的`min(timeout, interval)`會在1.0秒就把A判失敗；Refresh則會讓同一TTL等到固定5秒才送下一包；兩者都不符合這個時間軸。

## 17. 建議實作順序

| 階段 | 內容 | 先決條件 | 完成定義 | 不應同時混入 |
|---|---|---|---|---|
| 0 | 建立baseline tests與scripted fake transport | 無 | 現行snapshot/serializer golden + cadence regression test可執行 | 新UI功能 |
| 1 | 抽出`ProbeOutcome/ProbeSpec/ProbeId/IClock/IProbeTransport` | 階段0 | 現行同步transport可透過interface跑，行為不變 | 非同步重寫 |
| 2 | 實作nonblocking scheduler與fake transport全測 | 階段1 | 1s/3s、late、reset、stop、route change、cap測試全綠 | 真實Win32 API |
| 3 | 實作Windows async ICMP adapter與lifetime stress | 階段2 | Win7/10/11 v4/v6壓測；ASAN/Application Verifier無UAF/leak；停止可預期 | UDP/TCP |
| 4 | 切換production tracer，移除`min(timeout,interval)`與round `jthread` | 階段3 | 實機silent/slow/ECMP路徑cadence與loss正確；UI每包漸進更新 | 大型layout改版 |
| 5 | stats/schema修正 | 階段4 | sample stddev、outcomes、timestamps、schema v1/golden | percentile/graph |
| 6 | metadata queue/provider health與UI diff更新 | 階段4 | concurrency/rate/circuit breaker測試，UI不全表重建 | 新transport |
| 7 | headless report、TCP/UDP與進階history/graph | 核心穩定 | 各自有capability、integration/fuzz及文件 | 未測試的raw/MPLS |

### 最小P0變更集

| 順序 | 必做事項 | 理由 |
|---:|---|---|
| 1 | 先寫virtual-clock cadence測試，固定`interval=1s`, `timeout=3s`期望 | 防止用另一種「看起來變快」的修補再次改壞量測 |
| 2 | 取消`probe_timeout_ms = min(timeout, interval)` | 恢復timeout原本語意；但需和新scheduler一起發布，不能單獨回退造成round又變慢 |
| 3 | 移除round join barrier與per-round thread建立 | 讓每包completion獨立、避免silent hop拖住全路徑 |
| 4 | 建立stable async request lifetime + late discard | 解決Refresh式timer早於OS completion的風險 |
| 5 | sent/completed/inflight/local-error拆分 | 讓Loss不被在途包或本機資源錯誤污染 |
| 6 | Windows CI + start/stop/reset stress | 量測核心重寫沒有自動驗證不可發布 |

## 18. 原始碼責任對照

| Hyper檔案／群組 | Refresh對應 | mtr最接近對應 | 差異與建議 | 實作狀態 |
|---|---|---|---|---|
| `WinMTRNet-Tracing.cpp` | 同名；WinRT coroutine+async ICMP | `ui/net.c`, `ui/select.c`, `packet/probe*.c`, `packet/wait*.c` | Hyper檔案承擔過多；拆scheduler/transport/route/metadata四層 | 未開始 |
| `WinMTRNet-ClassDef.ixx` | 同名；較簡單host array/recursive mutex | `ui/net.h`, `ui/mtr.h`, `packet/probe.h` | 保留snapshot/session欄，新增ProbeId/outcome/inflight types | 未開始 |
| `WinMTRNet-Getters.cpp` | vector/getMax | `net_max/net_min` | 保留single snapshot API，淘汰多getter組合讀取 | 未開始 |
| `WinMTRSNetHost.ixx` | 基本單address統計 | `struct nethost`, `data_fields` | 合併Hyper 64-bit/Welford與mtr歷史/jitter；responder獨立stats | 未開始 |
| `WinMTRICMPUtils.ixx` | 362行async awaiter | `packet/probe_*`, `construct_*`, `deconstruct_*` | Hyper現在幾乎空；應成為安全Win32 transport module，但不可複製Refresh awaiter | 未開始 |
| `WinMTRIPUtils.ixx` | 同名 | `packet/sockaddr.*`, `ui/utils.*` | prefix/public判斷另拆table-driven address policy | 未開始 |
| `WinMTRDnsUtil.ixx` | WinRT resolver | `ui/dns.c` | target resolve與PTR resolver分離；共享bounded async service | 未開始 |
| `WinMTRNetworkData.cpp/.h` | 無 | `ui/asn.c`僅部分對應 | provider interface、HTTP client、parser、policy、formatter拆分；各自可測 | 未開始 |
| `WinMTRDialog-display.cpp` | 同名但較小 | `ui/curses.c`, `ui/gtk.c`, `ui/display.c` | 抽`DisplayRowBuilder`與`LayoutMetrics`純函式；UI只apply diff | 未開始 |
| `WinMTRDialog-StateMachine.cpp` | 同名 | `ui/select.c`的pause/grace/action | 保留UI states，底層改session phase event，不以timer猜worker狀態 | 未開始 |
| `WinMTRDialog-tracing.cpp` | target resolve/trace啟動 | `ui/mtr.c` target loop | 候選選擇移到TargetResolver/SessionController | 未開始 |
| `WinMTRDialog-exporter.cpp` | TXT/HTML基本匯出 | `ui/report.c`, `ui/raw.c`, `ui/split.c` | 每格式獨立serializer+golden；新增schema元資料與NDJSON | 未開始 |
| `WinMTRDialog-registry.cpp` | Registry/history/options | `MTR_OPTIONS`/CLI | 建settings schema/migration；CLI precedence文件化 | 未開始 |
| `WinMTRDialog-ClassDef.ixx` | 同名 | `ui/display.h`, `ui/mtr.h` | 減少大型Dialog state；services以owner objects組合 | 未開始 |
| `WinMTROptions.ixx` | 少量基本options | `ui/mtr.c`, `man/mtr.8.in` | model/validation與dialog view分離；option descriptor生成help/docs | 未開始 |
| `IWinMTROptionsProvider.ixx` | 3個virtual getter | `struct mtr_ctl` | session開始只取immutable validated options；不在每包跨virtual讀取 | 未開始 |
| `CWinMTRCommandLineParser.ixx` | 寬鬆小型parser | `ui/mtr.c`, portability getopt | 建純parser library、strict exit codes、全GUI options等價CLI | 未開始 |
| `WinMTRProperties.cpp/.h` | 基本節點dialog | TUI report rows | 接immutableNodeDetails VM；支援停止後/ECMP/error/source | 未開始 |
| `WinMTRStatusBar.ixx` | 同名 | terminal status | 保留custom pane，但補UIA/high contrast | 未開始 |
| `WinMTRBranding.h` | 無 | configure/package metadata | 改單一product config生成resource/header/manifest/version | 未開始 |
| `WinMTR.rc`, `resource.h` | 同名英文resource | GTK/terminal strings | 保留zh-TW resource；CI檢查orphan/missing IDs | 未開始 |
| `WinMTRMain.cpp/.h`, `WinMTRGlobal.*` | 同名 | `ui/mtr.c` main | 增加headless entry point但共用core；初始化/退出採structured task owner | 未開始 |
| `WinMTRWSAhelper.ixx` | 同名 | socket init在packet backend | 保留RAII；只由transport/runtime services擁有 | 未開始 |
| `WinMTRUtils.ixx` | 少量defaults | defaults散於`ui/mtr.c/.h` | constants改typed units；不要以同一常數同時代表send cadence與timeout | 未開始 |
| `WinMTRHelp.ixx`, `WinMTRLicense.ixx`, `WinMTRVerUtil.ixx` | 同名 | man page/version/COPYING | 保留；help由option descriptors部分生成，version單一來源 | 未開始 |
| `CMakeLists.txt`, `WinMTR.vcxproj`, `WinMTR.sln` | 同名 | `configure.ac`, `Makefile.am` | CMake作權威；修正x64與Win32/ARM64設定漂移，CI建置 | 未開始 |
| `app.manifest`, `targetver.h` | 偏Win10/OneCore | N/A | 保留Win7+PerMonitorV2策略；新增hardening/compat驗證 | 未開始 |
| `README.md`, `CUSTOMIZATION.md` | 短README | README/man/SECURITY/FORMATS/NEWS | 加STATISTICS、SCHEMA、SECURITY、UPSTREAM_NOTES、CHANGELOG | 未開始 |
| `.github/workflows/megalinter.yml` | 相同 | `.github/workflows/test.yaml` | 增Windows build/test workflow；縮MegaLinter write permissions | 未開始 |

## 19. 最終優先決策表

| 排名 | 決策 | 選擇來源 | 為何不是另外兩版 | 預期成果 |
|---:|---|---|---|---|
| 1 | 重寫probe scheduler，使send cadence與timeout獨立 | **mtr語意 + 自訂Win32實作** | Hyper round barrier錯誤耦合；Refresh async request生命週期不足以直接採用；mtr Unix transport不能原封搬入MFC | 真正每秒每TTL一包，仍允許每包等3秒以上 |
| 2 | 在任何新功能前建立fake transport/clock與P0 regression tests | **自訂，參考mtr tests** | Hyper/Refresh都沒有可執行測試；mtr沒有Windows MFC scheduler fixture | 從根本防止「變快但量測錯」 |
| 3 | sent/completed/inflight/outcome重新建模 | **mtr + 自訂** | Hyper/Refresh的xmit其實在完成時才增加；mtr模型較好但32-bit且UI語意仍可更清楚 | Loss只反映真正network timeout，不受pending/local error污染 |
| 4 | 保留Hyper UI、公網資訊、ECMP、在地化與匯出 | **Hyper** | 另外兩版缺少這些產品能力 | 不犧牲已完成的修改版價值 |
| 5 | stats採Hyper 64-bit/Welford底座，補mtr sample stddev/history/jitter | **混合** | 任一單版都不完整 | 長跑不overflow，公式明確，可做近期視窗/圖表 |
| 6 | metadata改bounded queue/provider state machine | **自訂，保留Hyper政策** | Hyper detached threads可放大；Refresh無功能；mtr DNS hash不足以涵蓋HTTPS來源治理 | 查詢不拖慢probe、不轟炸provider、隱私可控 |
| 7 | 修正build/CI/support matrix | **Hyper平台 + mtr測試紀律** | 現有兩個WinMTR fork的CI都只lint，不編譯Windows；mtr CI是Linux | 每次commit至少確定Windows x64能build、core tests全過 |
| 8 | 再新增TCP/UDP、history graph、headless report | **mtr功能 + Windows adapters** | 在P0核心未穩前加入只會擴大debug面 | 有節奏地擴充，而不是把Unix code硬塞進GUI |

## 20. 主要原始碼證據

| 主題 | Hyper | Refresh | mtr／官方文件 |
|---|---|---|---|
| 探測排程 | [`WinMTRNet-Tracing.cpp`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRNet-Tracing.cpp) | [`WinMTRNet-Tracing.cpp`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRNet-Tracing.cpp) | [`ui/select.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/select.c)、[`ui/net.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/net.c) |
| ICMP request／lifetime | 同上；同步呼叫 | [`WinMTRICMPUtils.ixx`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRICMPUtils.ixx) | [IcmpSendEcho2](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmpsendecho2)、[Icmp6SendEcho2](https://learn.microsoft.com/en-us/windows/win32/api/icmpapi/nf-icmpapi-icmp6sendecho2) |
| outstanding probes／timeout | round-local probes | TTL coroutine | [`packet/probe.h`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/packet/probe.h)、[`packet/wait_unix.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/packet/wait_unix.c) |
| packet解碼與protocol | Windows API解析 | Windows API解析 | [`packet/deconstruct_unix.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/packet/deconstruct_unix.c)、[`packet/construct_unix.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/packet/construct_unix.c) |
| 統計／ECMP | [`WinMTRSNetHost.ixx`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRSNetHost.ixx) | [`WinMTRSNetHost.ixx`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRSNetHost.ixx) | [`ui/net.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/net.c)、[`ui/mtr.h`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/mtr.h) |
| UI與row呈現 | [`WinMTRDialog-display.cpp`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRDialog-display.cpp) | [`WinMTRDialog-display.cpp`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRDialog-display.cpp) | [`ui/curses.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/curses.c)、[`ui/gtk.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/gtk.c) |
| 公網／hop metadata | [`WinMTRNetworkData.cpp`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRNetworkData.cpp) | 無 | [`ui/asn.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/asn.c) |
| DNS | [`WinMTRDnsUtil.ixx`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRDnsUtil.ixx) | [`WinMTRDnsUtil.ixx`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRDnsUtil.ixx) | [`ui/dns.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/dns.c) |
| 匯出 | [`WinMTRDialog-exporter.cpp`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTRDialog-exporter.cpp) | [`WinMTRDialog-exporter.cpp`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/WinMTRDialog-exporter.cpp) | [`ui/report.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/report.c)、[`FORMATS`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/FORMATS) |
| CLI/options | [`CWinMTRCommandLineParser.ixx`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/CWinMTRCommandLineParser.ixx)、[`WinMTROptions.ixx`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/WinMTROptions.ixx) | 同名檔案 | [`ui/mtr.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/ui/mtr.c)、[`man/mtr.8.in`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/man/mtr.8.in) |
| 建置 | [`CMakeLists.txt`](https://github.com/HyperNetworkTech/WinMTR-Hyper/blob/42460157cf41c251f2496041732ca17b8f16f89d/CMakeLists.txt) | [`CMakeLists.txt`](https://github.com/leeter/WinMTR-refresh/blob/932c8133a6973a2fa44fbcbc1cf066717b929748/CMakeLists.txt) | [`configure.ac`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/configure.ac)、[`Makefile.am`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/Makefile.am) |
| 安全 | `app.manifest` asInvoker、README | `app.manifest` asInvoker | [`SECURITY`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/SECURITY)、[`packet/packet.c`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/packet/packet.c) |
| tests/fuzz/CI | MegaLinter workflow，無test target | 同 | [`test/`](https://github.com/traviscross/mtr/tree/7b017733aef06bb3d8e3573b2e964cc876644fad/test)、[`fuzz/`](https://github.com/traviscross/mtr/tree/7b017733aef06bb3d8e3573b2e964cc876644fad/fuzz)、[`test.yaml`](https://github.com/traviscross/mtr/blob/7b017733aef06bb3d8e3573b2e964cc876644fad/.github/workflows/test.yaml) |

## 21. 範圍與限制

| 項目 | 說明 |
|---|---|
| 「最新」定義 | 本報告在比較日期以各repository預設`master`的淺層clone HEAD固定commit；未使用release tag取代HEAD。後續upstream commit不會自動反映在本文。 |
| 實機驗證 | 本輪重點是原始碼比較與建議，沒有把三套程式各自在所有OS/網路條件執行。涉及Windows async ICMP取消/關handle的hard-cleanup行為，實作前必須在Win7/10/11做專門壓測，不應只靠推論。 |
| mtr平台差異 | mtr最成熟的pending-probe實作主要是Unix socket/event loop；採用的是其狀態機與量測語意，不代表其C程式可直接貼進Windows ICMP API。 |
| Refresh風險措辭 | 本報告指出的是「5秒應用timer可能早於OS async request完成，因此生命週期缺少足夠保證」；這是需要修正/驗證的高風險設計，不宣稱每次執行都必然產生UAF。 |
| 功能完整性 | 表格已以repository、subsystem、使用者功能、選項、格式、測試與維護面做功能聯集盤點；不逐行列出純排版、註解或等價helper差異，因那些不構成獨立實作決策。 |

## 附錄 A：預設值與範圍逐項對照

| 設定 | Hyper | Refresh | mtr | 建議採用 | 實作狀態 |
|---|---|---|---|---|---|
| interval | 預設1.0s；0.1–60s | 預設/最小1.0s；最大120s | 預設1.0s；必須>0；非root不得<1s | 預設1.0s，0.1–60s；全域pps cap與均勻spacing | 未開始 |
| payload／packet size | payload預設64B；0–4,096B | payload預設/最小64B；最大32,768B | 完整packet預設64B；28–65,535B；負值隨機size | UI清楚分payload/total；一般上限4,096，進階按transport能力 | 未開始 |
| max hops | 預設30；1–64 | 固定30 | 預設30；1–255 | 預設30，GUI上限64；未來CLI可255 | 未開始 |
| probe timeout | 設定預設1,000ms；100–10,000ms；⚠目前實際取`min(timeout,interval)` | 固定應用timer 5,000ms；API async timeout參數不生效 | 預設10s | 預設3,000ms；100–30,000ms；與interval完全獨立 | 未開始 |
| cycles | 預設0=無限；0–100,000 | 無限至停止 | noninteractive預設10；interactive無限，`-c`覆蓋 | GUI預設無限；headless需明確finite或允許Ctrl+C | 未開始 |
| grace | 沒有獨立設定；Stop drain目前round | 沒有安全完整的獨立grace | 預設5s，可設定>0 | 預設5s；上限不超剩餘logical deadlines | 未開始 |
| ToS byte | 預設0；0–255 | 固定/無UI | 預設0；0–255 | 保留0–255並顯示DSCP/ECN解讀 | 未開始 |
| payload pattern | 預設32；-1隨機，0–255固定 | 固定32 | 預設0；-1隨機，0–255固定 | 預設32；保留-1與seed | 未開始 |
| DF | 預設true，可切換；IPv4使用 | 固定true | transport/platform實作，無相同GUI設定 | 保留，IPv6時標示不適用 | 未開始 |
| first/start TTL | 預設1；1..max | 固定1 | 預設1；下限1 | 保留 | 完成（既有） |
| minimum/due TTL | 預設0停用；可設至max | 無 | dueTTL預設0；>0且不可低於first | 採mtr清楚的「至少探至TTL」語意 | 未開始 |
| unknown limit | 預設5；1–64 | 無可調設定 | 預設12；下限1 | 預設10或12；可調1–64 | 未開始 |
| ECMP display limit | 預設8；1–128；保存上限128 | 無ECMP | 預設8；最大128；path保存128 | 維持Hyper 8/128，完整匯出不受display limit | 未開始 |
| reply cache | 預設0關閉；0–86,400s | 無 | 功能預設關；啟用參數必須>0，內部預置60s | 預設關；低流量模式建議60s並明示Skipped | 未開始 |
| responder metadata cache | 24h；最多2,048，正負結果同TTL | 無 | session hash，無明確TTL/容量政策 | 正結果24h、負結果10min、2,048 LRU | 未開始 |
| DNS PTR | 預設開 | 可開關，預設由registry/default | 預設開；`-n`關 | 預設開，快速主表切換name/IP/both | 未開始 |
| ASN/ISP | 預設開，可獨立於DNS | 無 | 預設不選ipinfo欄；`-z/-y`開 | 若考量隱私，首次告知；DNS ASN可預設，HTTPS地理可獨立opt-in | 未開始 |
| IPv4 | 預設允許 | 可選且至少v4/v6一個 | AF_UNSPEC或平台default；`-4`強制 | 預設允許 | 未開始 |
| IPv6 | 預設允許 | 可選且至少v4/v6一個 | configure可停；`-6`強制 | 預設允許；顯示實際選定family | 未開始 |
| 公網資訊 | 預設開 | 無 | 無 | 保留，但首次說明外部來源並可一鍵關閉 | 未開始 |
| 公網refresh | 預設網路變更事件；另一模式固定30min，可設1–1,440min | 無 | 無 | 網路變更+5s debounce；固定模式30min；加手動與backoff | 未開始 |
| target history | 預設128；1–256 | 預設128；1–1,024 | 無內建GUI history | 保留128；上限256或512即可 | 未開始 |
| mtr cache timeout | 不適用（由reply cache秒數直接設定） | 不適用 | 內部預設60s，但只有`--cache`才啟用 | Hyper不需另設兩個互相混淆值 | 未開始 |
| mtr max display history | 無RTT ring | 無 | `SAVED_PINGS=400` | Hyper新增每hop 400點ring buffer | 未開始 |
| maximum outstanding | 同一round最多64；跨round不重疊 | 每TTL通常1，共約30 | `MAX_PROBES=10,240` | 依`hops×ceil(timeout/interval)`算動態需求，總上限128–4,096 | 未開始 |

---

**建議採用的整體組合：Hyper的產品層 + mtr的排程／路徑／測試原則 + 專為Windows ICMP API撰寫的新非同步request生命週期。** Refresh的每TTL獨立循環只能作需求佐證，不應直接成為新核心。
