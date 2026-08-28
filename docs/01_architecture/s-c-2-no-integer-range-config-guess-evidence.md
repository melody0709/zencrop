# S-C-2 evidence: kill integer-range config guess + exhaustiveness

Date: 2026-07-22  
Package: Stage 2 S-C Action/Tool/Panel/Control classification  
Slice: S-C-2  
Prior: S-C-1 `eae9e8ac`

## Intent

Delete residual Host **integer-range guessing** for command taxonomy (research §11.3: 不通过整数范围猜测 command 类型). The only product site used `ConfigConsume..ConfigColorWhite` ordinal range to dismiss more/tertiary panels for unhandled config clicks. Replaced with pure `ScreenshotCommandIsConfigControl`. Added exhaustiveness contract: every `ScreenshotToolbarCommand` enum value classifies to a known kind (no Unknown).

## Deleted Host authority

| Legacy pattern | Sole authority |
|---|---|
| `static_cast<int>(command)` range `ConfigConsume..ConfigColorWhite` | pure `ScreenshotCommandIsConfigControl` |

Color-picker drag/confirm/cancel and color swatches still handled by earlier dedicated branches (behavior preserved).

## Touch paths

- `src/screenshot/overlay/OverlayWindowScreenshot.ToolbarInteraction.inl` — range → pure kind
- `tests/test_screenshot_command_kind_contract.cpp` — exhaustiveness walk + ConfigControl span checks

## Verification

```text
build.bat --cmake --stop-running  → Build Success
ctest --test-dir build/cmake -L hermetic  → 58/58
```

Live product scan for integer-range command classification: **0**.

## KPI

| Metric | After |
|---|---:|
| hermetic | **58/58** |
| product integer-range command guess sites | **0** |
| Unknown-classified enum values | **0** |

## NEXT

S-C-3: next ownership domain among residual Host dual-own of command/action taxonomy (Local ActionCatalog wrappers net-delete if product still dual-calls Host Local vs pure; typed Action/Tool/Panel map; remaining Host switch on command that re-implements pure kind) — one domain per slice.
