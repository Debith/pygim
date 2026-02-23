# Arrow BCP Refactor Notes

This refactor is complete for runtime orchestration.

## Final direction

- Keep DataFrame persistence orchestration in native bindings.
- Keep Python stress script as a thin driver.
- Prefer Arrow C stream import in native code.
- Use IPC serialization only as native fallback.
- Avoid standalone helper-module conversion layers in the runtime path.
