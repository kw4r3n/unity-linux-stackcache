# unity-linux-stackcache

[日本語](#日本語) | [English](#english)

## 日本語

Linux版Unity Editorで、`ScriptableObject.CreateInstance` と `Object.Instantiate` が異常に遅くなる問題を回避する `LD_PRELOAD` ライブラリです。

### 症状

- 空の `ScriptableObject` を1つ作るだけで数ミリ秒かかる。エディタを長く使うほど悪化し、数十ミリ秒に達することもある。
- VRCFury、NDMF、Modular Avatarなど、アニメーターやアセットを大量に複製するツールのビルドが遅くなる。
- `new AnimatorState()` のように `new` で作るオブジェクトは速いが、`Instantiate` と `CreateInstance` だけが型を問わず遅い。

ある VRChat アバターでは、プレイモード突入が約15秒から約6.3秒に縮みました。Editor内のすべての処理に効くので、プレイモードだけでなく、アップロード時のビルドやアセットのインポートも速くなります。

### 原因

Unityの組み込みMonoは、`CreateInstance` と `Instantiate` のたびに `mono_thread_has_sufficient_execution_stack` でスタックの残りを確認します。この中で glibc の `pthread_getattr_np` が呼ばれます。

メインスレッドに対して、glibcは `/proc/self/maps` を全行読み、`sscanf` で解析してスタックの範囲を求めます。Unity Editorのメモリマッピングは数千行あり、使っているうちに増えていきます。そのため、オブジェクトを1つ作るたびに数千行のテキスト解析が走ります。

`perf record --call-graph dwarf` で確認した経路は次のとおりです。

```
Scripting::CreateScriptableObjectWithType
  → mono_thread_has_sufficient_execution_stack
    → pthread_getattr_np
      → __isoc23_sscanf (/proc/self/maps の各行)
```

Windows版とmacOS版のUnityは影響を受けません。

### 仕組み

`pthread_getattr_np` を差し替え、メインスレッドのスタック範囲を最初の1回だけglibcに問い合わせ、以降はその値を返します。メインスレッドのスタック範囲はプロセスの実行中に変わりません。メインスレッド以外の呼び出しは、そのままglibcに渡します。

### ビルドとテスト

```bash
make
make test
```

`make test` は、6,000個のマッピングがある状態で300回呼び出し、ライブラリの有無で時間を比べます。返るスタック範囲が毎回同じかも確認します。

```
without stackcache:
  300 calls: 1722.1 ms
with stackcache:
  300 calls: 0.3 ms
```

### 導入

```bash
make install
```

これで `~/.local/lib/unity-stackcache/stackcache.so` に入ります。次に、Unity Editorを起動するときに `LD_PRELOAD` を設定します。方法は2つあります。

**A. Unity Hubごと起動する**

Hubから起動したEditorに環境変数が引き継がれます。

```bash
LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so" unityhub
```

**B. Editorの起動スクリプトに書く**

ALCOMなどから `Editor/Unity` を起動していて、それがシェルスクリプトの場合は、`exec` の前に1行追加します。

```bash
export LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$UNITY_EDITOR" "$@"
```

`Editor/Unity` が実行ファイルそのものの場合は、元のファイルを `Unity.bin` に改名し、同じ場所に次のスクリプトを `Unity` として置きます(実行権限を付ける)。Unityを再インストールすると元に戻ります。

```bash
#!/usr/bin/env bash
export LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$(dirname "$0")/Unity.bin" "$@"
```

### 効いているかの確認

```bash
tr '\0' '\n' < /proc/$(pgrep -f 'Unity.*-projectPath' | head -1)/environ | grep LD_PRELOAD
```

Editor内で次を実行し、1,000回で数百ミリ秒以内なら有効です(無効なら数秒かかります)。

```csharp
var sw = System.Diagnostics.Stopwatch.StartNew();
var list = new System.Collections.Generic.List<UnityEngine.Object>();
for (int i = 0; i < 1000; i++) list.Add(UnityEngine.ScriptableObject.CreateInstance<UnityEngine.ScriptableObject>());
UnityEngine.Debug.Log($"CreateInstance x1000: {sw.ElapsedMilliseconds} ms");
foreach (var o in list) UnityEngine.Object.DestroyImmediate(o);
```

### 確認した環境

- Unity 2022.3.22f1(Linux)
- glibc 2.44(CachyOS)

### 戻し方

`LD_PRELOAD` の設定を外すだけで元に戻ります。

### AIの利用

原因の調査、コード、このREADMEの作成には AI(Anthropic の Claude Code)を使っています。計測と動作確認は「確認した環境」に書いた環境で、実際に行っています。

## English

An `LD_PRELOAD` library that fixes `ScriptableObject.CreateInstance` and `Object.Instantiate` being abnormally slow in the Unity Editor on Linux.

### Symptoms

- Creating a single empty `ScriptableObject` takes several milliseconds. It gets worse the longer the editor runs, sometimes reaching tens of milliseconds.
- Tools that clone many animator objects or assets, such as VRCFury, NDMF and Modular Avatar, build slowly.
- Objects created with `new` (for example `new AnimatorState()`) are fast; only `Instantiate` and `CreateInstance` are slow, for every type.

For one VRChat avatar, entering play mode went from about 15 s to about 6.3 s. It applies to everything in the editor, so upload builds and asset imports get faster too, not only play mode.

### Cause

Unity's embedded Mono checks the remaining stack with `mono_thread_has_sufficient_execution_stack` on every `CreateInstance` and `Instantiate`. That calls glibc's `pthread_getattr_np`.

For the main thread, glibc finds the stack bounds by reading every line of `/proc/self/maps` and parsing it with `sscanf`. The Unity Editor has thousands of memory mappings, and the count grows as it runs. So every object creation parses thousands of lines of text.

The path, as seen with `perf record --call-graph dwarf`:

```
Scripting::CreateScriptableObjectWithType
  → mono_thread_has_sufficient_execution_stack
    → pthread_getattr_np
      → __isoc23_sscanf (each line of /proc/self/maps)
```

Unity on Windows and macOS is not affected.

### How it works

It overrides `pthread_getattr_np`, asks glibc for the main thread's stack bounds once, and returns that value afterwards. The main thread's stack bounds do not change while the process runs. Calls for other threads go straight to glibc.

### Build and test

```bash
make
make test
```

`make test` calls the function 300 times with 6,000 mappings present and compares the time with and without the library. It also checks that the returned stack bounds never change.

```
without stackcache:
  300 calls: 1722.1 ms
with stackcache:
  300 calls: 0.3 ms
```

### Install

```bash
make install
```

This installs `~/.local/lib/unity-stackcache/stackcache.so`. Then set `LD_PRELOAD` when the Unity Editor starts, in one of two ways.

**A. Launch Unity Hub with it**

Editors started from the Hub inherit the variable.

```bash
LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so" unityhub
```

**B. Put it in the editor's launch script**

If you start `Editor/Unity` from ALCOM or similar and it is a shell script, add one line before the `exec`:

```bash
export LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$UNITY_EDITOR" "$@"
```

If `Editor/Unity` is the executable itself, rename it to `Unity.bin` and put this script in its place as `Unity` (make it executable). Reinstalling Unity undoes this.

```bash
#!/usr/bin/env bash
export LD_PRELOAD="$HOME/.local/lib/unity-stackcache/stackcache.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$(dirname "$0")/Unity.bin" "$@"
```

### Checking that it is active

```bash
tr '\0' '\n' < /proc/$(pgrep -f 'Unity.*-projectPath' | head -1)/environ | grep LD_PRELOAD
```

In the editor, run the following. It is active if 1,000 creations take a few hundred milliseconds or less (several seconds without it).

```csharp
var sw = System.Diagnostics.Stopwatch.StartNew();
var list = new System.Collections.Generic.List<UnityEngine.Object>();
for (int i = 0; i < 1000; i++) list.Add(UnityEngine.ScriptableObject.CreateInstance<UnityEngine.ScriptableObject>());
UnityEngine.Debug.Log($"CreateInstance x1000: {sw.ElapsedMilliseconds} ms");
foreach (var o in list) UnityEngine.Object.DestroyImmediate(o);
```

### Tested on

- Unity 2022.3.22f1 (Linux)
- glibc 2.44 (CachyOS)

### Removing it

Remove the `LD_PRELOAD` setting.

### Use of AI

The investigation, the code, and this README were made with AI (Anthropic's Claude Code). Measurements and testing were done for real on the environment listed under "Tested on".
