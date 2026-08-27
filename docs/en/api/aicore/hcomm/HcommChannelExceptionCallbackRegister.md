# HcommChannelExceptionCallbackRegister

## Function Description
Registers an exception callback for a target kernel before launch. When the kernel triggers an AIC Error, the framework only records the exception state and does not perform D2H, printing, or launch operations inside the callback.

## Function Prototype
```cpp
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `kernelSymbol` | Input | Kernel symbol used to bind the exception callback. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Registration succeeded. |
| `-1` | Registration failed. |

## Constraints
- Call this API before launching the target kernel.
- The callback only records the exception state. It does not perform device-to-host copy, printing, or kernel launch operations inside the exception callback.
- If registration is not performed, no exception state is recorded and a later dump call becomes a no-op.
