# 粵拼輸入法 · Yutping IME

一個適用於 Windows 10／11（x64）的粵拼輸入法。它透過 Windows Text Services Framework（TSF）在一般桌面程式輸入文字，向 Google Input Tools 的網路服務查詢繁體中文候選字。這是**非官方的獨立開源專案**，沒有使用或包含 Google Chrome 擴充功能的程式碼，也不隸屬於 Google。

> 目前仍是概念驗證版。建議先在記事本等一般程式試用；Google 服務或回應格式改變時，查字功能可能失效。

## 下載與安裝

1. [直接下載 Windows 安裝程式](https://github.com/eddylaucheukwa-cloud/yutping-ime/raw/main/dist/YutpingSetup.exe)（`YutpingSetup.exe`）。更新時亦可執行同一個檔案，原有設定會保留。
2. 雙擊 EXE，接受 Windows 管理員權限提示。程式會註冊輸入法，並在需要時加入「中文（繁體，香港）」語言；原有語言設定會保留。
3. 關閉並重開想使用輸入法的程式，按 `Win + Space` 選擇「粵拼輸入法 (Yutping IME)」。如果仍顯示舊版，請先儲存工作，從 Windows **登出再登入**。已開啟的程式可能仍把舊 DLL 留在記憶體；毋須反覆安裝。

安裝程式未有數碼簽署，Windows 可能顯示「未知發行者」警告。只支援 64 位元應用程式。解除安裝可到「設定 → 應用程式 → 已安裝的應用程式」選擇 **Yutping IME**。

## 如何使用

例如輸入 `neiho`：按 `Space` 選第一個中文候選字（通常是「你好」）；按 `Enter` 則直接輸出原本輸入的 `neiho`。

| 操作 | 按鍵 |
| --- | --- |
| 輸入粵拼 | `A`–`Z`；`'` 分隔音節 |
| 揀選當頁候選字 | `1`–`6` 或滑鼠點選 |
| 下一頁／上一頁 | `=`／`-`、`.`／`,`、`]`／`[`、`Page Down`／`Page Up`、右／左或下／上方向鍵 |
| 選第一個候選字 | `Space` |
| 輸出原始粵拼 | `Enter` |
| 刪除／取消組字 | `Backspace`／`Esc` |
| 切換中英文模式 | 單按 `Shift`；可在設定改為 `F12` |

候選窗會在組字期間顯示拼法、候選字和讀音註記；查詢下一個拼法時，舊候選字會暫時淡化，避免視窗每打一個字就消失。候選窗跟隨 Windows 應用程式的深色／淺色模式。中文模式下，一般標點符號會轉換為中文標點。

在開始功能表開啟「**粵拼輸入法設定**」，可選每頁顯示 4／6／9 個候選字、開關中文標點和常用字排序、選擇 Shift 或 F12 切換鍵，也可清除快取及常用字紀錄。這些設定適用於新輸入，毋須重新啟動電腦。

## 網路與資料

輸入的粵拼會傳送到 Google Input Tools 服務以取得候選字。候選結果會在目前 Windows 使用者的本機登錄檔快取七日；過期後會重新查詢，斷線時只能使用已有的快取。常用字選擇紀錄亦只存於本機。可隨時在設定視窗清除兩種資料。本專案沒有內建完整離線詞庫。

## 從原始碼編譯

需要 Visual Studio 2022（含 C++ 桌面開發工具）、Windows SDK 和 CMake 3.20 或更新版本。在 **x64 Developer Command Prompt for VS 2022** 中執行：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

編譯後會產生 `dist\YutpingSetup.exe`。CMake 亦會產生內容相同的 `YutpingUpdate.exe` 供本機使用；公開下載只需一個 EXE。若目前 Windows 已載入舊 DLL，編譯時可改用另一個全新的 build 目錄，避免檔案被佔用。

## 已知限制

- 只提供 x64 DLL；32 位元程式尚未支援。
- 沒有數碼簽署、自動更新或完整離線字庫。
- 候選窗位置取決於目標程式提供的 TSF／Windows 插入點座標；某些程式可能無法準確定位。
- Google 服務不是本專案所控制；網路服務、可用性和候選結果隨時可能改變。

## 授權

請參閱 [LICENSE](LICENSE)。
