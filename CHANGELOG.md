# Changelog

本專案以使用者可觀察的行為變更為主；量測公式、timeout、interval、路徑終止與
匯出 schema 的語意變更必須明確列出。

## Unreleased

### Changed

- 探測器改為每 TTL 獨立 1 秒 cadence，TTL slot 在 interval 內均勻分布；timeout
  與發送間隔分離，慢回覆不再讓整輪探測停住。
- loss 固定為 `timed_out / completed`；in-flight、本機錯誤、scheduler/cache skip
  與 late completion 不再誤算網路丟包。
- 標準差改為樣本標準差；分離平滑 jitter 與最近 RTT 差。
- 路徑到達、unknown tail、frontier exploration、縮短 hysteresis 與 destination
  後晚到 completion 有明確生命週期。
- metadata 改為 bounded `jthread` pool、正負 cache、取消、總時間預算、provider
  cooldown/backoff；依產品需求以 ipinfo 為主要節點與公網資訊來源。
- 主表按封包 completion 增量更新；連續等待／無回覆 TTL 合併顯示；DPI、欄寬、
  高對比、內容導向視窗尺寸與手動調整行為修正。
- 移除首次啟動的外部查詢詢問視窗；查詢仍可由「選項」或 CLI 關閉。
- JSON schema v1 加入 session、outcome、公式與排程診斷；CSV 加入 session/row/IP
  metadata 與公式注入防護；所有檔案改為原子替換並回報 Win32 錯誤。
- CLI 補齊 GUI 追蹤選項並嚴格驗證；解析錯誤使用 process exit code 2。
- 目標 DNS 解析加入 5 秒 deadline 與取消；雙棧 A/AAAA 候選以 IPv4 優先、
  250 ms 交錯的 bounded 平行探測選出實際可用位址，不再逐一跑完整首輪路徑。

### Quality

- x64 正式組態採 `/W4 /WX /sdl`；Win32/ARM64 checked-in VS 組態 fail-fast。
- CI 同時驗證 checked-in solution 與 CMake graph，執行離線 core、schema/golden、
  import/manifest gate，nightly 執行 AddressSanitizer core tests。
