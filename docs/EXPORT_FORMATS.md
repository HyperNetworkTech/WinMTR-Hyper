# 匯出格式與相容性

WinMTR Hyper 可複製純文字／HTML，並寫出 TXT、HTML、CSV 與 JSON。所有檔案
以 UTF-8 產生；TXT 與 CSV 加 BOM 以改善 Windows 試算表相容性。

## 寫入可靠性

檔案先完整寫入目的檔同目錄的暫存檔、flush 並關閉，再用
`MoveFileEx(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 原子替換。
失敗時保留原目的檔，刪除暫存檔，錯誤視窗顯示完整路徑、Win32 原因與代碼。

## CSV

CSV 每列固定以前綴欄位 `target,session_id,row_kind,ip,started_at_utc,
ended_at_utc,duration_ms` 開始，再接 14 個穩定的量測欄。`row_kind` 為 `hop`
或 `responder`；所有 ECMP responders 都會匯出，不受畫面顯示上限影響。

逗號、雙引號、CR/LF 依 RFC 4180 方式 quote。為避免 Excel／試算表將外部文字
當公式執行，以 `= + - @` 起始的文字會先加單引號。這是安全的 Excel profile；
需要逐 byte 原值的 machine consumer 應使用 JSON。

## JSON schema v1

正式 schema 位於 [`schema/winmtr-report-v1.json`](schema/winmtr-report-v1.json)。
JSON 包含 session ID、UTC 起訖時間、duration、統計公式識別、完整 hop outcomes、
排程診斷、metadata source/failure reason，以及 stable responder ID、hit count 與
last-seen sequence。`cancelled` 是 grace 到期或使用者停止後取消的已送探測，
不納入 loss。無回覆樣本的 RTT／jitter／stddev 欄位為 `null`。

v1 只允許向後相容地新增 optional 欄位。刪除、改名、型別或既有語意改變都必須
升 major schema version。`tests/golden/winmtr-report-v1.json` 覆蓋 CJK、non-BMP
Unicode、ECMP、無回覆、`null` 與大型 64-bit 計數器；CI 會離線驗證它與 schema。
